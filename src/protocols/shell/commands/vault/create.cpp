#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"

#include "db/query/vault/Vault.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/vault/Waiver.hpp"

#include "storage/Manager.hpp"

#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/APIKey.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "identities/model/User.hpp"

#include "log/Registry.hpp"
#include "config/Registry.hpp"
#include "db/encoding/interval.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::identities::model;
using namespace vh::storage;
using namespace vh::config;
using namespace vh::sync::model;
using namespace vh::db::encoding;

static constexpr const auto* SYNC_STRATEGY_HELP = R"(
Sync Strategy Options:
  cache  - Local cache of S3 bucket. Changes are uploaded to S3 on demand.
           Downloads are served from cache if available, otherwise fetched from S3.
  sync   - Two-way sync between local and S3. Changes in either location are propagated
           to the other during sync operations.
  mirror - One-way mirror of local to S3. Local changes are uploaded to S3,
           but changes in S3 are not downloaded locally.

)";

static constexpr const auto* LOCAL_CONFLICT_POLICY_HELP = R"(
On-Sync-Conflict Policy Options:
  overwrite  - In case of conflict, overwrite the remote with the local version.
  keep_both  - In case of conflict, keep both versions by renaming the remote.
  ask        - Prompt the user to resolve conflicts during sync operations.
)";

static constexpr const auto* REMOTE_CONFLICT_POLICY_HELP = R"(
On-Sync-Conflict Policy Options:
  keep_local  - In case of conflict, keep the local version and overwrite the remote.
  keep_remote - In case of conflict, keep the remote version and overwrite the local.
  ask         - Prompt the user to resolve conflicts during sync operations.

)";

static constexpr const auto& SYNC_INTERVAL_HELP = R"(
Sync Interval:

  S3 Vaults: Defines how often the system will synchronize changes between the local cache and the S3 bucket.
  Local Vaults: Sync is primarily event-driven, but this interval sets how often the system checks for filesystem changes.

  ⚠️  S3 Vaults only: Setting a very short interval (e.g., every few seconds) may lead to increased API usage and potential costs.
      Choose an interval that balances timeliness with cost-effectiveness.

  ⚠️  Setting a very short interval may lead to high CPU usage due to frequent filesystem checks.
      Choose an interval that balances timeliness with system performance.

  Format: A number followed by a time unit:
      s - seconds
      m - minutes
      h - hours
      d - days

  Examples:
    30s  - Every 30 seconds
    10m  - Every 10 minutes
    1h   - Every 1 hour
  Default is 15 minutes (15m).
)";

static CommandResult finish_vault_create(const CommandCall& call, std::shared_ptr<Vault>& v,
                                         const std::shared_ptr<Policy>& s) {
    try {
        const auto [okToProceed, waiver] = handle_encryption_waiver({call, v, false});
        if (!okToProceed) return invalid("vault create: user did not accept encryption waiver");

        v = vh::runtime::Deps::get().storageManager->addVault(v, s);
        if (waiver) db::query::vault::Waiver::addWaiver(waiver);

        return ok("\nSuccessfully created new vault!\n" + to_string(v));
    } catch (const std::exception& e) {
        if (db::query::vault::Vault::vaultExists(v->name, v->owner_id))
            vh::runtime::Deps::get().storageManager->removeVault(v->id);

        return invalid("\nvault create error: " + std::string(e.what()) + "\n");
    }
}

static std::string stripLeadingDashes(const std::string& s) {
    size_t pos = 0;
    while (pos < s.size() && s[pos] == '-') ++pos;
    return s.substr(pos);
}

static CommandResult handle_vault_create_interactive(const CommandCall& call) {
    const auto& io = call.io;

    std::shared_ptr<Vault> v;
    std::shared_ptr<Policy> sync;

    const auto helpOptions = std::vector<std::string>{"help", "h", "?"};

    const auto usage = resolveUsage({"vault", "create"});
    validatePositionals(call, usage);

    const auto type = io->prompt("Select vault type (local/s3) [local]:", "local");
    if (type == "local") {
        v = std::make_shared<Vault>();
        v->type = VaultType::Local;
    } else if (type == "s3") {
        v = std::make_shared<S3Vault>();
        v->type = VaultType::S3;
    } else return invalid("vault create: invalid vault type");

    v->name = io->prompt("Enter vault name (required):");
    if (v->name.empty()) return invalid("vault create: vault name is required");

    v->description = io->prompt("Enter vault description (optional):");

    const auto quotaStr = io->prompt("Enter vault quota (e.g. 10G, 500M) or leave blank for unlimited:");
    v->quota = quotaStr.empty() ? 0 : parseSize(quotaStr);

    const auto ownerPrompt = io->prompt("Enter owner user ID or username (leave blank for yourself):");
    const auto owner = resolveOwner(call, usage);
    v->owner_id = owner->id;

    if (v->owner_id != call.user->id && !call.user->canCreateVaults())
        return invalid("vault create: user ID " + std::to_string(call.user->id) +
            " does not have permission to create vaults for other users");

    if (v->type == VaultType::Local) {
        auto fSync = std::make_shared<LocalPolicy>();

        auto conflictStr = io->prompt(
            "Enter on-sync-conflict policy (overwrite/keep_both/ask) [overwrite] --help for details:", "overwrite");
        while (conflictStr == "help") {
            io->print(LOCAL_CONFLICT_POLICY_HELP);
            conflictStr = io->prompt("Enter on-sync-conflict policy (overwrite/keep_both/ask) [overwrite]:",
                                     "overwrite");
        }
        fSync->conflict_policy = fsConflictPolicyFromString(conflictStr);
        sync = fSync;
    }

    if (v->type == VaultType::S3) {
        const auto s3Vault = std::static_pointer_cast<S3Vault>(v);
        const auto apiKeyStr = io->prompt("Enter API key name or ID (required):");
        if (apiKeyStr.empty()) return invalid("vault create: API key is required for S3 vaults");

        std::shared_ptr<APIKey> apiKey;
        if (const auto apiKeyIdOpt = parseUInt(apiKeyStr)) apiKey = db::query::vault::APIKey::getAPIKey(*apiKeyIdOpt);
        else apiKey = db::query::vault::APIKey::getAPIKey(apiKeyStr);

        if (!apiKey) return invalid("vault create: API key not found: " + apiKeyStr);
        if (apiKey->user_id != v->owner_id) {
            if (!call.user->canManageAPIKeys())
                return invalid(
                    "vault create: user ID " + std::to_string(call.user->id) +
                    " does not have permission to assign API keys to other users vaults"
                    );

            const auto ownerUser = db::query::identities::User::getUserById(v->owner_id);
            if (!ownerUser) return invalid("vault create: owner user ID not found: " + std::to_string(v->owner_id));

            if (apiKey->user_id != ownerUser->id && !ownerUser->canManageAPIKeys())
                return invalid("vault create: API key '" + apiKeyStr + "' does not belong to user ID " +
                               std::to_string(v->owner_id));
        }

        s3Vault->api_key_id = apiKey->id;

        s3Vault->bucket = io->prompt("Enter S3 bucket name (required):");
        if (s3Vault->bucket.empty()) return invalid("vault create: S3 bucket name is required");

        auto strategyStr = io->prompt("Enter sync strategy (cache/sync/mirror) [cache] --help for details:",
                                      "cache");
        while (std::ranges::find(helpOptions.begin(), helpOptions.end(), stripLeadingDashes(strategyStr)) !=
               helpOptions.end()) {
            io->print(SYNC_STRATEGY_HELP);
            strategyStr = io->prompt("Enter sync strategy (cache/sync/mirror) [cache]:", "cache");
               }
        const auto rsync = std::make_shared<RemotePolicy>();
        rsync->strategy = strategyFromString(strategyStr);

        auto conflictStr = io->prompt(
            "Enter on-sync-conflict policy (keep_local/keep_remote/ask) [ask] --help for details:", "ask");
        while (std::ranges::find(helpOptions.begin(), helpOptions.end(), stripLeadingDashes(conflictStr)) !=
               helpOptions.end()) {
            io->print(REMOTE_CONFLICT_POLICY_HELP);
            conflictStr = io->prompt("Enter on-sync-conflict policy (keep_local/keep_remote/ask) [ask]:", "ask");
               }
        rsync->conflict_policy = rsConflictPolicyFromString(conflictStr);

        sync = rsync;

        s3Vault->encrypt_upstream = io->confirm("Enable upstream encryption? (yes/no) [yes]", false);
    }

    auto interval = io->prompt("Enter sync interval (e.g. 30s, 10m, 1h) [15m] --help for details:", "15m");
    while (std::ranges::find(helpOptions.begin(), helpOptions.end(), stripLeadingDashes(interval)) != helpOptions.
           end()) {
        io->print(SYNC_INTERVAL_HELP);
        interval = io->prompt("Enter sync interval (e.g. 30s, 10m, 1h) [15m]:", "15m");
           }
    sync->interval = parseSyncInterval(interval);

    return finish_vault_create(call, v, sync);
}

CommandResult commands::vault::handle_vault_create(const CommandCall& call) {
    if (!call.user->canCreateVaults()) return invalid(
        "vault create: user ID " + std::to_string(call.user->id) + " does not have permission to create vaults");

    if (hasFlag(call, "interactive")) return handle_vault_create_interactive(call);

    const auto usage = resolveUsage({"vault", "create"});
    validatePositionals(call, usage);
    const auto owner = resolveOwner(call, usage);

    const auto type = parseVaultType(call);
    std::shared_ptr<Vault> vault;
    if (*type == VaultType::Local) vault = std::make_shared<Vault>();
    else if (*type == VaultType::S3) vault = std::make_shared<S3Vault>();
    else return invalid("vault create: unknown vault type");

    vault->type = *type;
    vault->name = call.positionals[0];
    vault->owner_id = owner->id;
    assignDescIfAvailable(call, usage, vault);
    assignQuotaIfAvailable(call, usage, vault);

    if (db::query::vault::Vault::vaultExists(vault->name, owner->id)) return invalid(
        "vault create: vault with name '" + vault->name + "' already exists for user ID " + std::to_string(owner->id));

    std::shared_ptr<Policy> sync;
    if (vault->type == VaultType::Local) sync = std::make_shared<LocalPolicy>();
    else if (vault->type == VaultType::S3) sync = std::make_shared<RemotePolicy>();
    else return invalid("vault create: unknown vault type");

    parseSync(call, usage, vault, sync);
    parseS3API(call, usage, vault, owner->id, true);

    return finish_vault_create(call, vault, sync);
}
