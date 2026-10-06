#pragma once

// Shared protocol for vaulthalla preview helpers (see the derive seam in the rich-preview spec).
//
// argv: <helper> <command> --input-size N [--max-output-bytes M] [command options]
// fd 3: range-pull socketpair (request: u64 offset, u32 length, u32 reserved = 0, little endian;
//       reply: u32 n, then n bytes; n < length only at EOF).
// fd 1: artifact bytes (FramedOutput for multi-file artifacts).
// fd 4: exactly one JSON result line.

#include <nlohmann/json.hpp>

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

enum class ExitCode : int { Ok = 0, Internal = 1, InvalidInput = 2, LimitExceeded = 3, Unsupported = 4 };

struct LimitExceeded : std::runtime_error { using std::runtime_error::runtime_error; };
struct InvalidInput  : std::runtime_error { using std::runtime_error::runtime_error; };
struct Unsupported   : std::runtime_error { using std::runtime_error::runtime_error; };

class RangeClient {             // pulls plaintext from the daemon over kRangeFd
public:
    RangeClient(int fd, uint64_t size);
    [[nodiscard]] uint64_t size() const;
    std::size_t read(uint64_t offset, std::span<uint8_t> out);   // short only at EOF; throws on protocol error
    std::vector<uint8_t> readAll(uint64_t maxBytes);              // throws LimitExceeded over cap

private:
    int fd_;
    uint64_t size_;
};

void writeResult(const nlohmann::json& result);                   // to kResultFd, one line, then close

class OutputSink {               // stdout, counts bytes, enforces --max-output-bytes
public:
    explicit OutputSink(uint64_t maxBytes);
    void write(std::span<const uint8_t> bytes);                   // throws LimitExceeded when over
    [[nodiscard]] uint64_t written() const;

private:
    uint64_t maxBytes_;
    uint64_t written_ = 0;
};

class FramedOutput {             // multi-file outputs (HLS)
public:
    explicit FramedOutput(OutputSink& sink);
    void file(std::string_view name, std::span<const uint8_t> bytes);
    void finish();               // writes name_len = 0

private:
    OutputSink& sink_;
    bool finished_ = false;
};

struct Args {
    std::string command;
    std::unordered_map<std::string, std::string> options;
    uint64_t inputSize = 0;
    uint64_t maxOutputBytes = 0;
};

// Parses --input-size/--max-output-bytes, applies the sandbox, maps exceptions to ExitCode + result JSON.
// The sandbox gets allowGpuDevices when a --hwaccel option other than "software" is present.
int runMain(int argc, char** argv, const std::function<nlohmann::json(const Args&, RangeClient&, OutputSink&)>& body);

}
