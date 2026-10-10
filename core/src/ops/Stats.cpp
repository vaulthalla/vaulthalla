#include "ops/Stats.hpp"

#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "rbac/permission/admin/Stats.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/resolver/admin/all.hpp"

#include <exception>
#include <string>

namespace vh::ops::stats {

namespace {

bool ownsVault(const Actor& actor, const std::uint32_t vaultId) {
    try {
        return db::query::vault::Vault::getVaultOwnerId(vaultId) == actor->id;
    } catch (const std::exception&) {
        return false;
    }
}

}

bool canViewSystem(const Actor& actor) {
    if (!actor) return false;
    using Perm = rbac::permission::admin::StatsPermissions;
    return rbac::resolver::Admin::has<Perm>({.user = actor, .permission = Perm::View});
}

void requireSystem(const Actor& actor, const std::string_view what) {
    requireActor(actor);
    if (!canViewSystem(actor)) throw Denied("viewing " + std::string(what) + " needs the admin.stats.view permission");
}

bool canViewVault(const Actor& actor, const std::uint32_t vaultId) {
    if (!actor) return false;
    if (ownsVault(actor, vaultId)) return true;
    using Perm = rbac::permission::admin::VaultPermissions;
    return rbac::resolver::Admin::has<Perm>({.user = actor, .permissions = {Perm::View, Perm::ViewStats}, .vault_id = vaultId});
}

void requireVault(const Actor& actor, const std::uint32_t vaultId, const std::string_view what) {
    requireActor(actor);
    if (!canViewVault(actor, vaultId))
        throw Denied("you do not have permission to view " + std::string(what) + " for this vault");
}

}
