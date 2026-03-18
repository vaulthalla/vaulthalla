#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"

#include "db/query/vault/Vault.hpp"
#include "db/query/vault/Waiver.hpp"
#include "db/query/sync/Policy.hpp"

#include "storage/Manager.hpp"

#include "vault/model/Vault.hpp"
#include "identities/User.hpp"

#include "config/Registry.hpp"
#include "CommandUsage.hpp"

#include "rbac/resolver/admin/all.hpp"

#include <string>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::config;

CommandResult commands::vault::handle_vault_update(const CommandCall& call) {
    constexpr const auto* ERR = "vault update";

    const auto usage = resolveUsage({"vault", "update"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, ERR);
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    using Perm = rbac::permission::admin::VaultPermissions;
    if (!rbac::resolver::Admin::has<Perm>({
        .user = call.user,
        .permission = Perm::Edit,
        .vault_id = vault->id
    })) return invalid("vault update: you do not have permission to edit this vault");

    assignDescIfAvailable(call, usage, vault);
    assignQuotaIfAvailable(call, usage, vault);
    assignOwnerIfAvailable(call, usage, vault);

    const auto sync = db::query::sync::Policy::getSync(vault->id);
    parseSync(call, usage, vault, sync);
    parseS3API(call, usage, vault, false);

    const auto [okToProceed, waiver] = handle_encryption_waiver({call, vault, true});
    if (!okToProceed) return invalid("vault create: user did not accept encryption waiver");
    if (waiver) db::query::vault::Waiver::addWaiver(waiver);

    db::query::vault::Vault::upsertVault(vault, sync);

    return ok("Successfully updated vault!\n" + to_string(vault));
}


CommandResult commands::vault::handle_vault_delete(const CommandCall& call) {
    constexpr const auto* ERR = "vault delete";

    const auto usage = resolveUsage({"vault", "delete"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, ERR);
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    using Perm = rbac::permission::admin::VaultPermissions;
    if (!rbac::resolver::Admin::has<Perm>({
        .user = call.user,
        .permission = Perm::Remove,
        .vault_id = vault->id
    })) return invalid("vault delete: you do not have permission to remove this user");

    runtime::Deps::get().storageManager->removeVault(vault->id);

    return ok("Successfully deleted vault '" + vault->name + "' (ID: " + std::to_string(vault->id) + ")\n");
}
