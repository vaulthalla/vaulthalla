#pragma once

// Helper side of the daemon <-> converter protocol (see preview/derive/Runner.hpp for the daemon side).
//
//   argv   <helper> <command> --input-size N --max-output-bytes N [command options]   ("--key value" or "--key=value";
//          --input-size defaults to 0, --max-output-bytes to 512 MiB)
//   fd 0   /dev/null
//   fd 1   artifact byte stream (runMain moves it aside and points fd 1 at stderr, so library chatter on
//          std::cout can never corrupt the artifact)
//   fd 2   diagnostics (captured by the daemon, truncated)
//   fd 3   range-pull socketpair: request frame (16 bytes, little endian) u64 offset, u32 length, u32 reserved=0;
//          reply u32 n followed by n bytes (n < length only at EOF; n = 0 means EOF)
//   fd 4   result: exactly one JSON line
//   exit   0 ok, 1 internal, 2 invalid_input, 3 limit_exceeded, 4 unsupported, 5 sandbox_unavailable (the helper
//          refused to run because Landlock or seccomp could not be applied); a signal is a crash
//
// Hidden option, every command: --sandbox-test-no-landlock 1 makes the helper behave as on a kernel without Landlock
// (it then refuses with sandbox_unavailable). Tests use it; it can only turn a run into a refusal.

#include "sandbox.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vh::helpers {

constexpr int kRangeFd = 3;   // range-pull socketpair
constexpr int kResultFd = 4;  // one JSON line

// Largest single range request a helper sends; the daemon refuses anything above kMaxRangeRequest.
constexpr uint32_t kRangeChunk = 1u << 20;
constexpr uint32_t kMaxRangeRequest = 8u << 20;
constexpr std::size_t kRangeRequestSize = 16;

enum class ExitCode : int {
    Ok = 0, Internal = 1, InvalidInput = 2, LimitExceeded = 3, Unsupported = 4, SandboxUnavailable = 5
};

struct LimitExceeded : std::runtime_error { using std::runtime_error::runtime_error; };
struct InvalidInput  : std::runtime_error { using std::runtime_error::runtime_error; };
struct Unsupported   : std::runtime_error { using std::runtime_error::runtime_error; };

// Pulls plaintext from the daemon over kRangeFd.
class RangeClient {
public:
    RangeClient(int fd, uint64_t size);

    [[nodiscard]] uint64_t size() const { return size_; }

    // Short only at EOF; throws std::runtime_error on a protocol error (daemon gone, oversized reply).
    std::size_t read(uint64_t offset, std::span<uint8_t> out);

    // The whole input. Throws LimitExceeded when the declared size is above maxBytes and std::runtime_error
    // when the daemon delivers fewer bytes than it declared.
    std::vector<uint8_t> readAll(uint64_t maxBytes);

private:
    int fd_;
    uint64_t size_;
};

// To kResultFd, one line, then close. Only the first call writes.
void writeResult(const nlohmann::json& result);

// The artifact stream (the original fd 1), counting bytes and enforcing --max-output-bytes.
class OutputSink {
public:
    explicit OutputSink(uint64_t maxBytes);

    void write(std::span<const uint8_t> bytes);   // throws LimitExceeded when over
    [[nodiscard]] uint64_t written() const { return written_; }
    [[nodiscard]] uint64_t limit() const { return max_; }

private:
    int fd_;
    uint64_t max_;
    uint64_t written_ = 0;
};

// Multi-file outputs (HLS): u32 name_len, name, u64 data_len, data, repeated; terminated by name_len = 0.
class FramedOutput {
public:
    explicit FramedOutput(OutputSink& sink);

    void file(std::string_view name, std::span<const uint8_t> bytes);
    void finish();

private:
    OutputSink& sink_;
    bool finished_ = false;
};

struct Args {
    std::string command;
    std::unordered_map<std::string, std::string> options;   // "--key value" pairs, key without the dashes
    uint64_t inputSize = 0;
    uint64_t maxOutputBytes = 0;

    // Typed accessors for command options; a malformed value is a usage error (std::invalid_argument).
    [[nodiscard]] uint64_t u64(const std::string& key, uint64_t fallback) const;
    [[nodiscard]] std::string str(const std::string& key, const std::string& fallback) const;
};

// Parses --input-size/--max-output-bytes, moves the artifact stream off fd 1, applies the sandbox, runs body and
// maps its outcome to ExitCode + result JSON (ok results get "ok": true; every result carries "sandbox").
// sandboxFor picks the sandbox options per invocation; without it, allowGpuDevices is set when a --hwaccel option
// other than "software" is present. Hardware devices must be opened inside body (after the sandbox), never before:
// landlock_restrict_self confines only the calling thread, so threads a driver starts earlier would escape it.
// A helper that cannot install both Landlock and its seccomp filter refuses to run (error "sandbox_unavailable",
// exit 5): it never parses hostile input without either.
int runMain(int argc, char** argv,
            const std::function<nlohmann::json(const Args&, RangeClient&, OutputSink&)>& body,
            const std::function<sandbox::Options(const Args&)>& sandboxFor = {});

}
