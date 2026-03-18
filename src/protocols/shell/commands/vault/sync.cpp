#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "sync/Controller.hpp"
#include "storage/Engine.hpp"
#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/Policy.hpp"
#include "db/encoding/interval.hpp"
#include "CommandUsage.hpp"
#include "rbac/resolver/vault/all.hpp"

#include <optional>
#include <string>
#include <vector>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::sync::model;
using namespace vh::db::encoding;

static CommandResult handle_vault_sync(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync";

    const auto usage = resolveUsage({"vault", "sync"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, ERR);
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncActionPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Trigger,
        .vault_id = vault->id
    })) return invalid("vault sync: you do not have permission to trigger a sync for this vault");

    runtime::Deps::get().syncController->runNow(vault->id);

    return ok("Vault sync initiated for '" + vault->name + "' (ID: " + std::to_string(vault->id) + ")");
}

static CommandResult handle_vault_sync_update(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync update";

    const auto usage = resolveUsage({"vault", "sync", "update"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncConfigPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Edit,
        .vault_id = engine->vault->id
    }))

    if (const auto intervalOpt = optVal(call, "interval")) {
        try {
            engine->sync->interval = parseSyncInterval(*intervalOpt);
        } catch (const std::exception& e) {
            return invalid("vault sync update: " + std::string(e.what()));
        }
    }

    if (engine->vault->type == VaultType::Local) {
        if (const auto onSyncConflictOpt = optVal(call, usage->resolveOptional("on-sync-conflict")->option_tokens)) {
            const auto fsync = std::static_pointer_cast<LocalPolicy>(engine->sync);

            try {
                fsync->conflict_policy = fsConflictPolicyFromString(*onSyncConflictOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }
    } else if (engine->vault->type == VaultType::S3) {
        const auto rsync = std::static_pointer_cast<RemotePolicy>(engine->sync);

        if (const auto syncStrategyOpt = optVal(call, usage->resolveOptional("sync-strategy")->option_tokens)) {
            try {
                rsync->strategy = strategyFromString(*syncStrategyOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }

        if (const auto onSyncConflictOpt = optVal(call, usage->resolveOptional("on-sync-conflict")->option_tokens)) {
            try {
                rsync->conflict_policy = rsConflictPolicyFromString(*onSyncConflictOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }
    }

    if (engine->vault->type == VaultType::Local) {
        const auto fsync = std::static_pointer_cast<LocalPolicy>(engine->sync);
        return ok("Successfully updated local vault sync configuration!\n" + to_string(fsync));
    }

    if (engine->vault->type == VaultType::S3) {
        const auto rsync = std::static_pointer_cast<RemotePolicy>(engine->sync);
        return ok("Successfully updated S3 vault sync configuration!\n" + to_string(rsync));
    }

    return invalid("vault sync update: invalid sync configuration");
}

static CommandResult handle_vault_sync_info(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync info";

    const auto usage = resolveUsage({"vault", "sync", "info"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncConfigPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::View,
        .vault_id = engine->vault->id
    })) return invalid("vault sync info: you do not have permission to view the sync configuration for this vault");

    if (!engine->sync) return invalid("vault sync info: vault does not have a sync configuration");

    return ok(to_string(engine->sync));
}

static bool isVaultSyncMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"vault", "sync", cmd}, input);
}

CommandResult commands::vault::handle_sync(const CommandCall& call) {
    const auto [arg, subcall] = descend(call);
    if (call.positionals.size() == 1) return handle_vault_sync(call);
    if (isVaultSyncMatch("update", arg)) return handle_vault_sync_update(subcall);
    if (isVaultSyncMatch("info", arg)) return handle_vault_sync_info(subcall);

    return invalid(call.constructFullArgs(), "vault sync: unknown subcommand: '" + std::string(arg) + "'");
}
