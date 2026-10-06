#include "preview/derive/Sandbox.hpp"

#include "config/Registry.hpp"

namespace vh::preview::derive {

Limits limitsFromConfig() {
    const auto& derive = config::Registry::get().preview.derive;
    Limits limits;
    limits.maxAddressSpaceBytes = static_cast<uint64_t>(derive.max_ram_mb) << 20;
    limits.maxCpuSeconds = derive.max_cpu_seconds;
    limits.wallTimeout = std::chrono::seconds(derive.wall_timeout_seconds);
    limits.maxOutputBytes = static_cast<uint64_t>(derive.max_output_mb) << 20;
    return limits;
}

}
