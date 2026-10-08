#include "rbac/PolicyEpoch.hpp"

#include <atomic>

namespace vh::rbac {

namespace {
std::atomic<uint64_t> epoch{1};
}

uint64_t policyEpoch() noexcept { return epoch.load(std::memory_order_acquire); }
void bumpPolicyEpoch() noexcept { epoch.fetch_add(1, std::memory_order_acq_rel); }

}
