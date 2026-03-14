#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/util/argsHelpers.hpp"

#include "runtime/Deps.hpp"
#include "log/Registry.hpp"
#include "db/query/vault/Vault.hpp"

#include "storage/Manager.hpp"
#include "storage/Engine.hpp"

#include "vault/model/Vault.hpp"
#include "../../../../../include/rbac/role/Vault.hpp"
#include "../../../../../include/identities/User.hpp"

#include "config/Registry.hpp"
#include "CommandUsage.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::rbac::model;
using namespace vh::identities::model;
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

    if (!call.user->canManageVaults() && vault->owner_id != call.user->id) return invalid(
        "vault info: you do not have permission to view this vault");

    if (!call.user->isAdmin() && !call.user->canListVaultData(vault->id)) return invalid(
        "vault info: you do not have permission to view this vault's data");

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

    const auto canListAll = call.user->isAdmin() || call.user->canManageVaults();

    auto vaults = canListAll
                      ? db::query::vault::Vault::listVaults(typeFilter, parseListQuery(call))
                      : db::query::vault::Vault::listUserVaults(call.user->id, typeFilter, parseListQuery(call));

    if (!canListAll) {
        for (const auto& [_, r] : call.user->vault) {
            if (r->canList({})) {
                const auto vault = runtime::Deps::get().storageManager->getEngine(r->vault_id)->vault;
                if (!vault) continue;
                if (vault->owner_id == call.user->id) continue; // Already added
                vaults.push_back(vault);
            }
        }
    }

    return ok(to_string(vaults));
}
