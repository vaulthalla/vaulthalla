#pragma once

#include <cstdint>

// A process-wide counter bumped by every authorization-relevant mutation that short-lived authorization caches
// depend on (share link update/revoke/token rotation, vault role template edits, vault role assignments and
// overrides). A cache entry recorded under an older epoch is treated as a miss.
namespace vh::rbac {

[[nodiscard]] uint64_t policyEpoch() noexcept;
void bumpPolicyEpoch() noexcept;

}
