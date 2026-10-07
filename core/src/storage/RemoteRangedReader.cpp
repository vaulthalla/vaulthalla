#include "storage/RemoteRangedReader.hpp"

#include "crypto/util/Gcm.hpp"
#include "log/Registry.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/ScopedS3RequestUsageCapture.hpp"
#include "storage/s3/Controller.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>

namespace vh::storage {

namespace ranged_reader_detail {

uint64_t windowCount(const uint64_t size, const uint64_t window) {
    return size == 0 ? 1 : (size + window - 1) / window;
}

uint64_t effectiveWindow(const RangedReadLimits& limits) {
    // Whole 16-byte blocks keep every window start block-aligned (not required by CTR positioning, but tidy).
    const auto w = std::max<uint64_t>(limits.window_bytes, 16);
    return w - (w % 16);
}

}

uint64_t RemoteRangedReader::defaultMaxGetRequests(const uint64_t plaintextSize, const RangedReadLimits& limits) {
    if (limits.max_get_requests) return *limits.max_get_requests;
    return 2 * ranged_reader_detail::windowCount(plaintextSize, ranged_reader_detail::effectiveWindow(limits)) + 8;
}

uint64_t RemoteRangedReader::defaultMaxDownloadedBytes(const uint64_t plaintextSize, const RangedReadLimits& limits) {
    if (limits.max_downloaded_bytes) return *limits.max_downloaded_bytes;
    return 2 * (plaintextSize + crypto::util::AES_TAG_SIZE + ranged_reader_detail::effectiveWindow(limits));
}

RemoteRangedReader::RemoteRangedReader(Params params) : params_(std::move(params)) {
    if (!params_.controller) throw std::invalid_argument("RemoteRangedReader needs an S3 controller");
    if (!params_.engine) throw std::invalid_argument("RemoteRangedReader needs its cloud engine");
    if (params_.etag.empty()) throw std::invalid_argument("RemoteRangedReader needs the object's ETag");
    params_.limits.window_bytes = ranged_reader_detail::effectiveWindow(params_.limits);
    params_.limits.cached_windows = std::max<std::size_t>(params_.limits.cached_windows, 1);
    maxGets_ = defaultMaxGetRequests(params_.plaintextSize, params_.limits);
    maxBytes_ = defaultMaxDownloadedBytes(params_.plaintextSize, params_.limits);
    usage_ = params_.initialUsage;
}

RemoteRangedReader::~RemoteRangedReader() {
    if (!params_.reservation) return;
    std::scoped_lock lock(mutex_);
    params_.reservation->commit(usage_);
}

s3::S3GatewayUpstreamUsage RemoteRangedReader::usage() const {
    std::scoped_lock lock(mutex_);
    return usage_;
}

void RemoteRangedReader::fetchRange(const uint64_t first, const uint64_t last, std::vector<uint8_t>& out) {
    const auto length = last - first + 1;
    if (usage_.get_requests + 1 > maxGets_ || usage_.downloaded_bytes + length > maxBytes_)
        throw ContentUnavailable(fmt::format(
            "Ranged remote read budget exhausted for {} ({} GETs / {} bytes used, caps {} / {})",
            params_.objectKey.string(), usage_.get_requests, usage_.downloaded_bytes, maxGets_, maxBytes_));

    out.clear();
    out.reserve(static_cast<std::size_t>(length));

    s3::S3RequestBudget remaining;
    remaining.max_get_requests = maxGets_ - usage_.get_requests;
    remaining.max_downloaded_bytes = maxBytes_ - usage_.downloaded_bytes;

    s3::GetObjectOptions options;
    options.range = std::make_pair(first, last);
    options.if_match = params_.etag;
    options.max_body_bytes = length;

    // The capture is thread-local, so it lives only around this GET; the reader may be driven from several threads
    // over its life. Its usage is folded into the reader's running total whatever the outcome.
    ScopedS3RequestUsageCapture capture(*params_.engine, remaining);
    const auto fold = [&] {
        auto used = capture.usage();
        used.source = "preview_ranged";
        usage_.merge(used);
    };

    try {
        (void)params_.controller->streamObject(params_.objectKey, options, [&](const std::span<const uint8_t> bytes) {
            out.insert(out.end(), bytes.begin(), bytes.end());
        });
    } catch (const s3::ConditionalRequestFailed&) {
        fold();
        throw IntegrityError("Remote object changed: " + params_.objectKey.string());
    } catch (const s3::ObjectNotFound&) {
        fold();
        throw ContentUnavailable("Remote object no longer exists: " + params_.objectKey.string());
    } catch (const s3::RequestBudgetExceeded& e) {
        fold();
        throw ContentUnavailable(fmt::format("S3 request budget refused a ranged read of {}: {}",
                                             params_.objectKey.string(), e.what()));
    } catch (...) {
        fold();
        throw;
    }
    fold();

    if (out.size() != length)
        throw IntegrityError(fmt::format("Remote object {} returned {} bytes for a {}-byte range",
                                         params_.objectKey.string(), out.size(), length));
}

const RemoteRangedReader::Window& RemoteRangedReader::window(const uint64_t index) {
    if (const auto it = std::ranges::find(windows_, index, &Window::index); it != windows_.end()) {
        windows_.splice(windows_.begin(), windows_, it);
        return windows_.front();
    }

    const auto first = index * params_.limits.window_bytes;
    const auto last = std::min(first + params_.limits.window_bytes, params_.plaintextSize) - 1;
    Window fresh{.index = index, .bytes = {}};
    fetchRange(first, last, fresh.bytes);

    windows_.push_front(std::move(fresh));
    while (windows_.size() > params_.limits.cached_windows) windows_.pop_back();
    return windows_.front();
}

std::size_t RemoteRangedReader::read(const uint64_t offset, const std::span<uint8_t> out) {
    std::scoped_lock lock(mutex_);
    if (offset >= params_.plaintextSize || out.empty()) return 0;

    const auto n = static_cast<std::size_t>(std::min<uint64_t>(out.size(), params_.plaintextSize - offset));
    std::size_t done = 0;
    while (done < n) {
        const auto pos = offset + done;
        const auto index = pos / params_.limits.window_bytes;
        const auto& win = window(index);
        const auto inWindow = static_cast<std::size_t>(pos - index * params_.limits.window_bytes);
        const auto take = std::min(n - done, win.bytes.size() - inWindow);
        const std::span<const uint8_t> source(win.bytes.data() + inWindow, take);
        const auto target = out.subspan(done, take);
        if (params_.key) {
            const crypto::util::GcmIv iv(params_.iv.data(), crypto::util::AES_IV_SIZE);
            crypto::util::gcmCtrDecryptAt(params_.key->bytes(), iv, pos, source, target);
        } else {
            std::memcpy(target.data(), source.data(), take);
        }
        done += take;
    }
    return n;
}

std::vector<uint8_t> RemoteRangedReader::readAllAuthenticated(const uint64_t maxBytes) {
    std::scoped_lock lock(mutex_);
    const auto size = params_.plaintextSize;
    if (size > maxBytes) throw std::length_error("Content exceeds the allowed size");
    if (size == 0) return {};

    const auto stored = size + (params_.key ? crypto::util::AES_TAG_SIZE : 0);
    std::vector<uint8_t> sealed;
    fetchRange(0, stored - 1, sealed);
    if (!params_.key) return sealed;

    std::vector<uint8_t> plaintext(static_cast<std::size_t>(size));
    const crypto::util::GcmIv iv(params_.iv.data(), crypto::util::AES_IV_SIZE);
    const std::span<const uint8_t, crypto::util::AES_TAG_SIZE> tag(sealed.data() + size, crypto::util::AES_TAG_SIZE);
    if (!crypto::util::gcmDecrypt(params_.key->bytes(), iv, {sealed.data(), static_cast<std::size_t>(size)}, tag,
                                  plaintext))
        throw IntegrityError("Remote object failed authentication: " + params_.objectKey.string());
    return plaintext;
}

}
