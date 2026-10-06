#pragma once

#include <string>
#include <vector>

namespace vh::helpers::sandbox {

struct Options { bool allowGpuDevices = false; std::vector<std::string> extraReadOnlyPaths; };
struct Report { bool landlock = false; int landlockAbi = 0; bool seccomp = false; };

// Landlock read-only allowlist + seccomp denylist. Never weakens on failure: if Landlock is unavailable it
// reports landlock=false (logged by the daemon); a seccomp failure throws.
Report apply(const Options& options);

}
