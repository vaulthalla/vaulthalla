#include "share/Principal.hpp"

namespace vh::share {

bool Principal::isExpired(const std::time_t now) const { return expires_at != 0 && expires_at <= now; }

bool Principal::isActive(const std::time_t now) const {
    return !share_id.empty() && !share_session_id.empty() && vault_id != 0 && root_entry_id != 0 && !isExpired(now);
}

bool Principal::allows(const Operation op) const { return grant.allows(op); }

bool Principal::hasVault(const uint32_t vaultId) const noexcept { return vault_id == vaultId; }

}
