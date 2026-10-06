#include "common/protocol.hpp"
#include "common/sandbox.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <system_error>

#include <sys/socket.h>
#include <unistd.h>

namespace vh::helpers {

namespace protocol_detail {

constexpr uint32_t kMaxRequest = 1u << 20;  // one range request never asks for more than 1 MiB

void putLe(uint8_t* out, uint64_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint64_t getLe(const uint8_t* in, std::size_t bytes) {
    uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value |= static_cast<uint64_t>(in[i]) << (8 * i);
    return value;
}

void writeAll(int fd, const uint8_t* data, std::size_t len) {
    while (len > 0) {
        ssize_t n = ::send(fd, data, len, MSG_NOSIGNAL);
        if (n < 0 && errno == ENOTSOCK) n = ::write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "write");
        }
        data += n;
        len -= static_cast<std::size_t>(n);
    }
}

// Reads exactly len bytes; returns false on a clean EOF before the first byte.
bool readExact(int fd, uint8_t* data, std::size_t len) {
    std::size_t done = 0;
    while (done < len) {
        const ssize_t n = ::read(fd, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "range read");
        }
        if (n == 0) {
            if (done == 0) return false;
            throw std::runtime_error("range protocol: truncated reply");
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

uint64_t parseU64(const std::string& key, const std::string& value) {
    uint64_t out = 0;
    const auto* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(value.data(), end, out);
    if (ec != std::errc{} || ptr != end || value.empty())
        throw std::invalid_argument("--" + key + " expects an unsigned integer");
    return out;
}

std::string_view errorName(ExitCode code) {
    switch (code) {
        case ExitCode::InvalidInput: return "invalid_input";
        case ExitCode::LimitExceeded: return "limit_exceeded";
        case ExitCode::Unsupported: return "unsupported";
        default: return "internal";
    }
}

int fail(ExitCode code, const std::string& message) {
    writeResult({{"ok", false}, {"error", errorName(code)}, {"message", message}});
    return static_cast<int>(code);
}

constexpr uint64_t kDefaultMaxOutput = 512ull << 20;

}

RangeClient::RangeClient(const int fd, const uint64_t size) : fd_(fd), size_(size) {}

uint64_t RangeClient::size() const { return size_; }

std::size_t RangeClient::read(const uint64_t offset, std::span<uint8_t> out) {
    std::size_t total = 0;
    while (total < out.size()) {
        const uint64_t at = offset + total;
        if (at >= size_) break;
        const auto want = static_cast<uint32_t>(
            std::min<uint64_t>({out.size() - total, size_ - at, protocol_detail::kMaxRequest}));

        std::array<uint8_t, 16> request{};
        protocol_detail::putLe(request.data(), at, 8);
        protocol_detail::putLe(request.data() + 8, want, 4);
        protocol_detail::writeAll(fd_, request.data(), request.size());

        std::array<uint8_t, 4> header{};
        if (!protocol_detail::readExact(fd_, header.data(), header.size()))
            throw std::runtime_error("range protocol: daemon closed the channel");
        const auto n = static_cast<uint32_t>(protocol_detail::getLe(header.data(), 4));
        if (n > want) throw std::runtime_error("range protocol: reply longer than requested");
        if (n > 0 && !protocol_detail::readExact(fd_, out.data() + total, n))
            throw std::runtime_error("range protocol: daemon closed the channel");
        total += n;
        if (n < want) break;  // EOF
    }
    return total;
}

std::vector<uint8_t> RangeClient::readAll(const uint64_t maxBytes) {
    if (size_ > maxBytes) throw LimitExceeded("input larger than " + std::to_string(maxBytes) + " bytes");
    std::vector<uint8_t> data(size_);
    data.resize(read(0, data));
    return data;
}

void writeResult(const nlohmann::json& result) {
    static bool written = false;
    if (written) return;
    written = true;
    const std::string line = result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    try {
        protocol_detail::writeAll(kResultFd, reinterpret_cast<const uint8_t*>(line.data()), line.size());
    } catch (const std::exception&) {
        // No result channel (manual invocation): the exit code still carries the outcome.
    }
    ::close(kResultFd);
}

OutputSink::OutputSink(const uint64_t maxBytes) : maxBytes_(maxBytes) {}

void OutputSink::write(std::span<const uint8_t> bytes) {
    if (bytes.size() > maxBytes_ - std::min(written_, maxBytes_))
        throw LimitExceeded("output exceeds " + std::to_string(maxBytes_) + " bytes");
    protocol_detail::writeAll(STDOUT_FILENO, bytes.data(), bytes.size());
    written_ += bytes.size();
}

uint64_t OutputSink::written() const { return written_; }

FramedOutput::FramedOutput(OutputSink& sink) : sink_(sink) {}

void FramedOutput::file(const std::string_view name, std::span<const uint8_t> bytes) {
    if (finished_) throw std::logic_error("FramedOutput: file() after finish()");
    if (name.empty() || name.size() > 255) throw std::invalid_argument("FramedOutput: bad file name");
    std::vector<uint8_t> header(4 + name.size() + 8);
    protocol_detail::putLe(header.data(), name.size(), 4);
    std::memcpy(header.data() + 4, name.data(), name.size());
    protocol_detail::putLe(header.data() + 4 + name.size(), bytes.size(), 8);
    sink_.write(header);
    sink_.write(bytes);
}

void FramedOutput::finish() {
    if (finished_) return;
    finished_ = true;
    constexpr std::array<uint8_t, 4> terminator{};
    sink_.write(terminator);
}

int runMain(const int argc, char** argv,
            const std::function<nlohmann::json(const Args&, RangeClient&, OutputSink&)>& body) {
    std::signal(SIGPIPE, SIG_IGN);

    Args args;
    try {
        if (argc < 2 || argv[1][0] == '-') throw std::invalid_argument("usage: <helper> <command> --input-size N [options]");
        args.command = argv[1];
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (!arg.starts_with("--") || arg.size() < 3) throw std::invalid_argument("unexpected argument: " + std::string(arg));
            arg.remove_prefix(2);
            if (const auto eq = arg.find('='); eq != std::string_view::npos) {
                args.options[std::string(arg.substr(0, eq))] = std::string(arg.substr(eq + 1));
            } else {
                if (i + 1 >= argc) throw std::invalid_argument("--" + std::string(arg) + " expects a value");
                args.options[std::string(arg)] = argv[++i];
            }
        }
        if (const auto it = args.options.find("input-size"); it != args.options.end())
            args.inputSize = protocol_detail::parseU64(it->first, it->second);
        args.maxOutputBytes = protocol_detail::kDefaultMaxOutput;
        if (const auto it = args.options.find("max-output-bytes"); it != args.options.end())
            args.maxOutputBytes = protocol_detail::parseU64(it->first, it->second);
    } catch (const std::exception& e) {
        return protocol_detail::fail(ExitCode::Internal, std::string("usage: ") + e.what());
    }

    try {
        sandbox::Options sandboxOptions;
        if (const auto it = args.options.find("hwaccel"); it != args.options.end())
            sandboxOptions.allowGpuDevices = it->second != "software";
        const auto report = sandbox::apply(sandboxOptions);

        RangeClient range(kRangeFd, args.inputSize);
        OutputSink sink(args.maxOutputBytes);
        nlohmann::json result = body(args, range, sink);
        if (!result.is_object()) result = nlohmann::json{{"value", result}};
        result["ok"] = true;
        result["output_bytes"] = sink.written();
        result["sandbox"] = {{"landlock", report.landlock}, {"landlock_abi", report.landlockAbi}, {"seccomp", report.seccomp}};
        writeResult(result);
        return static_cast<int>(ExitCode::Ok);
    } catch (const InvalidInput& e) {
        return protocol_detail::fail(ExitCode::InvalidInput, e.what());
    } catch (const LimitExceeded& e) {
        return protocol_detail::fail(ExitCode::LimitExceeded, e.what());
    } catch (const Unsupported& e) {
        return protocol_detail::fail(ExitCode::Unsupported, e.what());
    } catch (const std::exception& e) {
        return protocol_detail::fail(ExitCode::Internal, e.what());
    } catch (...) {
        return protocol_detail::fail(ExitCode::Internal, "unknown error");
    }
}

}
