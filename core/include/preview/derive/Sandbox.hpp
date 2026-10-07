#pragma once

// Resource confinement the daemon imposes on a converter helper before exec (see Runner.hpp). The helper adds its
// own Landlock + seccomp confinement (core/tools/common/sandbox.hpp) on top; the daemon never links either.

#include <chrono>
#include <cstdint>

namespace vh::preview::derive {

struct Limits {
    uint64_t maxAddressSpaceBytes = 2048ull << 20;   // RLIMIT_AS
    uint64_t maxCpuSeconds = 300;                    // RLIMIT_CPU soft (SIGXCPU); hard = soft + 5 (SIGKILL)
    std::chrono::milliseconds wallTimeout{std::chrono::seconds(600)};   // SIGKILL of the process group
    uint64_t maxOutputBytes = 512ull << 20;          // artifact stream cap (also passed as --max-output-bytes)
    uint64_t maxOpenFiles = 64;                      // RLIMIT_NOFILE
    // Threads in the helper's thread group, sampled from /proc/<pid>/stat every poll tick (20 ms): above this the
    // process group is SIGKILLed (limit_exceeded). Not an rlimit: RLIMIT_NPROC counts every task of the uid,
    // including the daemon's own threads, and seccomp cannot count. The CAD helper denies clone outright; the
    // media helper needs threads (decoder/encoder threads are capped by --threads, drivers add a few), so this
    // bounds a runaway or hostile helper to one tick's worth of threads instead of exhausting the service's
    // TasksMax for the whole wall timeout.
    uint64_t maxThreads = 64;
};

// preview.derive.{max_ram_mb, max_cpu_seconds, wall_timeout_seconds, max_output_mb}.
[[nodiscard]] Limits limitsFromConfig();

}
