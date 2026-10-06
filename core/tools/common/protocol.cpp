#include "protocol.hpp"

#include <malloc.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <system_error>

namespace vh::helpers {

namespace {

int gArtifactFd = STDOUT_FILENO;   // runMain moves the artifact stream here
bool gResultWritten = false;

void putLe(uint8_t* out, uint64_t value, const std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i, value >>= 8) out[i] = static_cast<uint8_t>(value & 0xffu);
}

uint64_t getLe(const uint8_t* in, const std::size_t bytes) {
    uint64_t value = 0;
    for (std::size_t i = bytes; i > 0; --i) value = (value << 8) | in[i - 1];
    return value;
}

void writeAll(const int fd, const uint8_t* data, std::size_t size) {
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "write");
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
}

// Reads exactly size bytes; false on EOF before the first byte, throws on EOF mid-message.
bool readExact(const int fd, uint8_t* data, const std::size_t size) {
    std::size_t got = 0;
    while (got < size) {
        const ssize_t n = ::read(fd, data + got, size - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "range read");
        }
        if (n == 0) {
            if (got == 0) return false;
            throw std::runtime_error("range protocol: truncated reply");
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

uint64_t parseU64(const std::string& key, const std::string& value) {
    uint64_t out = 0;
    const auto* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(value.data(), end, out);
    if (ec != std::errc{} || ptr != end || value.empty())
        throw std::invalid_argument("--" + key + " expects an unsigned integer, got '" + value + "'");
    return out;
}

constexpr uint64_t kDefaultMaxOutputBytes = 512ull << 20;

Args parseArgs(const int argc, char** argv) {
    if (argc < 2 || argv[1] == nullptr || argv[1][0] == '-')
        throw std::invalid_argument("usage: <helper> <command> [--input-size N] [--max-output-bytes N] [options]");
    Args args;
    args.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string_view key = argv[i];
        if (!key.starts_with("--") || key.size() < 3) throw std::invalid_argument("unexpected argument '" + std::string(key) + "'");
        key.remove_prefix(2);
        if (const auto eq = key.find('='); eq != std::string_view::npos) {   // --key=value
            args.options[std::string(key.substr(0, eq))] = std::string(key.substr(eq + 1));
            continue;
        }
        if (i + 1 >= argc) throw std::invalid_argument("missing value for --" + std::string(key));
        args.options[std::string(key)] = argv[++i];
    }
    // A command that never pulls input (e.g. capabilities) may omit --input-size.
    args.inputSize = args.u64("input-size", 0);
    args.maxOutputBytes = args.u64("max-output-bytes", kDefaultMaxOutputBytes);
    return args;
}

// Without an explicit choice, a helper asking for a hardware encoder (--hwaccel other than software) gets the GPU
// device allowance; everything else gets none.
sandbox::Options defaultSandboxOptions(const Args& args) {
    sandbox::Options options;
    if (const auto it = args.options.find("hwaccel"); it != args.options.end())
        options.allowGpuDevices = it->second != "software";
    return options;
}

nlohmann::json failure(const std::string& error, const std::string& message) {
    return {{"ok", false}, {"error", error}, {"message", message}};
}

}

// ── RangeClient ─────────────────────────────────────────────────────────────────────────────────────────────

RangeClient::RangeClient(const int fd, const uint64_t size) : fd_(fd), size_(size) {}

std::size_t RangeClient::read(const uint64_t offset, std::span<uint8_t> out) {
    std::size_t total = 0;
    while (total < out.size() && offset + total < size_) {
        const auto want = static_cast<uint32_t>(std::min<uint64_t>({out.size() - total, kRangeChunk,
                                                                    size_ - (offset + total)}));
        std::array<uint8_t, kRangeRequestSize> request{};
        putLe(request.data(), offset + total, 8);
        putLe(request.data() + 8, want, 4);
        // MSG_NOSIGNAL: a daemon that hung up is an error, not a SIGPIPE.
        std::size_t sent = 0;
        while (sent < request.size()) {
            const ssize_t n = ::send(fd_, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "range request");
            }
            sent += static_cast<std::size_t>(n);
        }

        std::array<uint8_t, 4> header{};
        if (!readExact(fd_, header.data(), header.size())) throw std::runtime_error("range protocol: daemon closed the channel");
        const auto n = static_cast<uint32_t>(getLe(header.data(), 4));
        if (n > want) throw std::runtime_error("range protocol: reply larger than the request");
        if (n > 0 && !readExact(fd_, out.data() + total, n)) throw std::runtime_error("range protocol: truncated reply");
        total += n;
        if (n < want) break;   // EOF
    }
    return total;
}

std::vector<uint8_t> RangeClient::readAll(const uint64_t maxBytes) {
    if (size_ > maxBytes)
        throw LimitExceeded("input is " + std::to_string(size_) + " bytes, limit " + std::to_string(maxBytes));
    std::vector<uint8_t> data(static_cast<std::size_t>(size_));
    if (const auto got = read(0, data); got != data.size())
        throw std::runtime_error("range protocol: input ended at " + std::to_string(got) + " of " +
                                 std::to_string(size_) + " bytes");
    return data;
}

// ── result / output ─────────────────────────────────────────────────────────────────────────────────────────

void writeResult(const nlohmann::json& result) {
    if (gResultWritten) return;
    gResultWritten = true;
    std::string line = result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    line.push_back('\n');
    try {
        writeAll(kResultFd, reinterpret_cast<const uint8_t*>(line.data()), line.size());
    } catch (const std::exception&) {
        // Nothing left to report to; the exit code still carries the outcome.
    }
    ::close(kResultFd);
}

OutputSink::OutputSink(const uint64_t maxBytes) : fd_(gArtifactFd), max_(maxBytes) {}

void OutputSink::write(const std::span<const uint8_t> bytes) {
    if (bytes.size() > max_ - written_)
        throw LimitExceeded("output exceeds " + std::to_string(max_) + " bytes");
    writeAll(fd_, bytes.data(), bytes.size());
    written_ += bytes.size();
}

FramedOutput::FramedOutput(OutputSink& sink) : sink_(sink) {}

void FramedOutput::file(const std::string_view name, const std::span<const uint8_t> bytes) {
    if (finished_) throw std::logic_error("FramedOutput already finished");
    if (name.empty() || name.size() > 4096) throw std::invalid_argument("framed output name must be 1..4096 bytes");
    std::array<uint8_t, 8> header{};
    putLe(header.data(), name.size(), 4);
    sink_.write(std::span<const uint8_t>(header.data(), 4));
    sink_.write(std::span(reinterpret_cast<const uint8_t*>(name.data()), name.size()));
    putLe(header.data(), bytes.size(), 8);
    sink_.write(header);
    sink_.write(bytes);
}

void FramedOutput::finish() {
    if (finished_) return;
    constexpr std::array<uint8_t, 4> terminator{};
    sink_.write(terminator);
    finished_ = true;
}

// ── Args ────────────────────────────────────────────────────────────────────────────────────────────────────

uint64_t Args::u64(const std::string& key, const uint64_t fallback) const {
    const auto it = options.find(key);
    return it == options.end() ? fallback : parseU64(key, it->second);
}

std::string Args::str(const std::string& key, const std::string& fallback) const {
    const auto it = options.find(key);
    return it == options.end() ? fallback : it->second;
}

// ── runMain ─────────────────────────────────────────────────────────────────────────────────────────────────

int runMain(const int argc, char** argv,
            const std::function<nlohmann::json(const Args&, RangeClient&, OutputSink&)>& body,
            const std::function<sandbox::Options(const Args&)>& sandboxFor) {
    std::signal(SIGPIPE, SIG_IGN);
    // Few arenas: with RLIMIT_AS, per-thread malloc arenas reserve address space that is never used.
    mallopt(M_ARENA_MAX, 2);

    // The artifact stream moves off fd 1; anything a library prints to stdout lands in the captured stderr.
    if (const int fd = ::fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 10); fd >= 0) {
        gArtifactFd = fd;
        ::dup2(STDERR_FILENO, STDOUT_FILENO);
    }

    Args args;
    try {
        args = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        writeResult(failure("internal", std::string("bad invocation: ") + e.what()));
        return static_cast<int>(ExitCode::Internal);
    }

    sandbox::Report report;
    try {
        report = sandbox::apply(sandboxFor ? sandboxFor(args) : defaultSandboxOptions(args));
    } catch (const std::exception& e) {
        report.detail += std::string("sandbox setup threw: ") + e.what();
    }
    const nlohmann::json sandboxJson = {
        {"landlock", report.landlock}, {"landlock_abi", report.landlockAbi}, {"seccomp", report.seccomp},
        {"open_write_denied", report.openWriteDenied}, {"detail", report.detail},
    };
    if (!report.seccomp) {
        auto result = failure("internal", "sandbox unavailable: " + report.detail);
        result["sandbox"] = sandboxJson;
        writeResult(result);
        return static_cast<int>(ExitCode::Internal);
    }

    const auto fail = [&](const ExitCode code, const std::string& error, const std::string& message) {
        auto result = failure(error, message);
        result["sandbox"] = sandboxJson;
        writeResult(result);
        return static_cast<int>(code);
    };

    try {
        RangeClient input(kRangeFd, args.inputSize);
        OutputSink output(args.maxOutputBytes);
        nlohmann::json result = body(args, input, output);
        if (!result.is_object()) result = nlohmann::json::object();
        if (!result.contains("ok")) result["ok"] = true;
        result["output_bytes"] = output.written();
        result["sandbox"] = sandboxJson;
        writeResult(result);
        return result["ok"].get<bool>() ? static_cast<int>(ExitCode::Ok) : static_cast<int>(ExitCode::Internal);
    } catch (const LimitExceeded& e) {
        return fail(ExitCode::LimitExceeded, "limit_exceeded", e.what());
    } catch (const InvalidInput& e) {
        return fail(ExitCode::InvalidInput, "invalid_input", e.what());
    } catch (const Unsupported& e) {
        return fail(ExitCode::Unsupported, "unsupported", e.what());
    } catch (const std::bad_alloc&) {
        return fail(ExitCode::LimitExceeded, "limit_exceeded", "memory limit reached");
    } catch (const std::exception& e) {
        return fail(ExitCode::Internal, "internal", e.what());
    } catch (...) {
        return fail(ExitCode::Internal, "internal", "unknown exception");
    }
}

}
