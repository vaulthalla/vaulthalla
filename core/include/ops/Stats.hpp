#pragma once

#include "ops/Actor.hpp"

#include <cstdint>
#include <string_view>

// Who may read stats (#166). Every stats.* ws command authorizes here; the stats models and queries beneath never do.
namespace vh::ops::stats {

// Server, daemon and system telemetry: the health dashboard and its severity, system health, thread pools, FUSE,
// database, operations, connections, storage, retention, trends, caches and the system-wide pricing totals.
// Needs admin.stats.view (super admins always pass).
[[nodiscard]] bool canViewSystem(const Actor& actor);
void requireSystem(const Actor& actor, std::string_view what);

// One vault's stats (stats.vault.*, stats.pricing.budget with a vault_id): the vault's owner, or an admin role with
// admin.vaults.<self|admin|user>.view + view_stats for that vault. A vault the actor may not see is refused the same
// way whether or not it exists. Vault-role members who are not the owner are not enough: activity, share and
// security stats cover the whole vault, past any path overrides that hide parts of it from them.
[[nodiscard]] bool canViewVault(const Actor& actor, std::uint32_t vaultId);
void requireVault(const Actor& actor, std::uint32_t vaultId, std::string_view what);

}
