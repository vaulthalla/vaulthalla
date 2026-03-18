#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"

#include "runtime/Deps.hpp"
#include "log/Registry.hpp"
#include "db/query/vault/Vault.hpp"

#include "storage/Manager.hpp"
#include "storage/Engine.hpp"

#include "vault/model/Vault.hpp"
#include "rbac/role/Vault.hpp"
#include "identities/User.hpp"

#include "config/Registry.hpp"
#include "CommandUsage.hpp"

#include "rbac/resolver/admin/all.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>

#include "rbac/permission/admin/Vaults.hpp"

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::rbac;
using namespace vh::identities;
using namespace vh::storage;
using namespace vh::config;
using namespace vh::crypto;

CommandResult commands::vault::handle_vault_info(const CommandCall& call) {
    constexpr const auto* ERR = "vault info";

    const auto usage = resolveUsage({"vault", "info"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, ERR);
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    using Perm = permission::admin::VaultPermissions;
    if (!resolver::Admin::has<Perm>({
        .user = call.user,
        .permission = Perm::View,
        .vault_id = vault->id
    })) return invalid("vault info: insufficient permissions");

    return ok(to_string(vault));
}

CommandResult commands::vault::handle_vaults_list(const CommandCall& call) {
    const auto usage = resolveUsage({"vault", "list"});
    validatePositionals(call, usage);

    const bool f_local = hasFlag(call, usage->resolveFlag("local")->aliases);
    const bool f_s3 = hasFlag(call, usage->resolveFlag("s3")->aliases);
    if (f_local && f_s3) return invalid("vaults: --local and --s3 are mutually exclusive");

    std::optional<VaultType> typeFilter = std::nullopt;
    if (f_local) typeFilter = VaultType::Local;
    else if (f_s3) typeFilter = VaultType::S3;

    std::vector<std::shared_ptr<Vault>> vaults;
    if (call.user->vaultsPerms().self.canView() && !(call.user->vaultsPerms().admin.canView() || call.user->vaultsPerms().user.canView()))
        vaults = db::query::vault::Vault::listUserVaults(call.user->id, typeFilter, parseListQuery(call));
    else {
        vaults = db::query::vault::Vault::listVaults(typeFilter, parseListQuery(call));
        for (const auto& v : vaults) {
            using Perm = permission::admin::VaultPermissions;
            if (!resolver::Admin::has<Perm>({
                .user = call.user,
                .permission = Perm::View,
                .vault_id = v->id
            })) std::erase(vaults, v);
        }
    }

    if (hasFlag(call, "json")) return ok(nlohmann::json(vaults).dump(4));
    return ok(to_string(vaults));
}
