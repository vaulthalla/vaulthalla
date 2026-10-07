#pragma once

// Runs one out-of-process converter ("helper") to completion. No OCCT/libav here: helpers are separate executables
// from optional packages (vaulthalla-preview-cad, vaulthalla-preview-media) under preview.derive.helper_dir.
//
// Child setup (fork, then async-signal-safe calls only): setsid (own process group), PR_SET_PDEATHSIG SIGKILL,
// PR_SET_NO_NEW_PRIVS, default signal dispositions and an empty mask, rlimits (AS, CPU, FSIZE=0, NOFILE, CORE=0),
// fds 0=/dev/null 1=artifact pipe 2=stderr pipe 3=range socketpair 4=result pipe and nothing else, cwd /,
// execve with the environment {LANG=C.UTF-8}.
//
// Protocol (helper side: core/tools/common/protocol.hpp):
//   argv  <helper> <command> --input-size N --max-output-bytes N [args...]
//   fd 3  range pull: request u64 offset, u32 length, u32 reserved=0 (LE); reply u32 n + n bytes (n < length only
//         at EOF). Served from the PlaintextReader: plaintext never touches the disk.
//   fd 1  artifact bytes, handed to the sink in order.
//   fd 4  one JSON line: {"ok":true,...} or {"ok":false,"error":"invalid_input|limit_exceeded|unsupported|internal",
//         "message":...}; helpers also report their confinement under "sandbox".
//   exit  0 ok, 1 internal, 2 invalid_input, 3 limit_exceeded, 4 unsupported; a signal is a crash.
//
// The daemon enforces the output cap and the wall-clock timeout itself (SIGKILL of the process group), always
// reaps the child, and never lets a helper failure throw: crashes, kills and protocol violations come back in the
// RunResult. A failed run may already have handed partial output to the sink; callers discard it unless ok().

#include "preview/derive/Sandbox.hpp"
#include "storage/PlaintextReader.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace vh::preview::derive {

inline constexpr std::string_view kCadHelper = "vaulthalla-preview-cad";
inline constexpr std::string_view kMediaHelper = "vaulthalla-preview-media";

inline constexpr int kRangeFd = 3;
inline constexpr int kResultFd = 4;
inline constexpr uint32_t kMaxRangeRequest = 8u << 20;   // larger requests are a protocol violation

// The helper executable is missing or not executable (package not installed): HTTP maps this to 503
// converter_unavailable.
class HelperUnavailable final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct RunRequest {
    std::filesystem::path executable;                 // usually Runner::helperPath(kCadHelper)
    std::string command;                              // e.g. "convert-step"
    std::vector<std::string> args;                    // command options, after the protocol options
    storage::PlaintextReader* input = nullptr;        // range-pull source; nullptr = empty input
    Limits limits;
    std::function<void(std::span<const uint8_t>)> sink;   // artifact bytes in order; may throw (aborts the run)
    std::stop_token stop;                             // stop requested: SIGKILL the process group ("cancelled")
};

struct RunResult {
    int exitCode = -1;                    // -1 when the helper died from a signal
    std::optional<int> signal;
    bool timedOut = false;                // wall-clock timeout: the process group was SIGKILLed
    bool outputLimitExceeded = false;     // the daemon-side output cap fired: the process group was SIGKILLed
    nlohmann::json result;                // the fd-4 JSON object, or null when none/invalid
    std::string stderrTail;               // last 64 KiB of stderr
    uint64_t outputBytes = 0;             // bytes handed to the sink
    uint64_t inputBytesServed = 0;        // range-pull bytes served
    std::string error;                    // daemon-side failure: exec failure, protocol violation

    [[nodiscard]] bool ok() const;
    // "" when ok(), else timeout | limit_exceeded | invalid_input | unsupported | crashed | protocol | cancelled |
    // internal.
    [[nodiscard]] std::string failureReason() const;
    // Short human-readable reason for logs and the negative cache.
    [[nodiscard]] std::string failureMessage() const;
};

class Runner {
public:
    // Spawns the helper and serves it until it exits. Throws HelperUnavailable when request.executable is not an
    // executable file, std::system_error when the pipes/fork fail. Exceptions from the reader (IntegrityError,
    // ContentUnavailable, std::system_error) or the sink are rethrown after the helper is killed and reaped.
    static RunResult run(const RunRequest& request);

    // <preview.derive.helper_dir>/<name>; name must be a plain file name (std::invalid_argument otherwise).
    [[nodiscard]] static std::filesystem::path helperPath(std::string_view name);
    // helperPath(name) exists, is a regular file and is executable by the daemon.
    [[nodiscard]] static bool helperAvailable(std::string_view name);
    [[nodiscard]] static bool isExecutable(const std::filesystem::path& path);
};

}
