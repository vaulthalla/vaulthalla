#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "config/Registry.hpp"

#include "db/query/vault/Vault.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/identities/User.hpp"

#include "storage/Manager.hpp"
#include "storage/s3/provider/Registry.hpp"

#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/APIKey.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "identities/User.hpp"
#include "db/encoding/interval.hpp"
#include "CommandUsage.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;

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
  keep_newest - In case of conflict, keep whichever version was modified last.
  ask         - Record the conflict and leave both versions as they are. Vaulthalla has no
                command to resolve a recorded conflict yet, so that file stops syncing until
                the vault's policy changes.

The default is vaults.s3.default_remote_conflict_policy in config.yaml.
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

namespace vh::protocols::shell::commands::vault {
    static std::string createdVaultMessage(const ops::vaults::VaultPtr& v) {
        return "\nSuccessfully created new vault!\n" + to_string(v);
    }

    static std::string stripLeadingDashes(const std::string& s) {
        size_t pos = 0;
        while (pos < s.size() && s[pos] == '-') ++pos;
        return s.substr(pos);
    }

    static CommandResult handle_vault_create_interactive(const CommandCall& call) {
        const auto& io = call.io;
        if (!io) return invalid("vault create --interactive: requires an interactive terminal (stdin is not a TTY, "
                                "or --yes/--non-interactive was given); pass the options as flags instead");

        const auto helpOptions = std::vector<std::string>{"help", "h", "?"};
        const auto isHelp = [&](const std::string& answer) {
            return std::ranges::find(helpOptions, stripLeadingDashes(answer)) != helpOptions.end();
        };

        const auto usage = resolveUsage({"vault", "create"});
        validatePositionals(call, usage);

        ops::vaults::Create req;
        const auto type = io->prompt("Select vault type (local/s3) [local]:", "local");
        if (type == "local") req.type = vh::vault::model::VaultType::Local;
        else if (type == "s3") req.type = vh::vault::model::VaultType::S3;
        else return invalid("vault create: invalid vault type");

        req.name = io->prompt("Enter vault name (required):");
        if (req.name.empty()) return invalid("vault create: vault name is required");
        req.description = io->prompt("Enter vault description (optional):");

        const auto quotaStr = io->prompt("Enter vault quota (e.g. 10G, 500M) or leave blank for unlimited:");
        req.quota = quotaStr.empty() ? 0 : parseSize(quotaStr);

        const auto ownerPrompt = io->prompt("Enter owner user ID or username (leave blank for yourself):");
        req.owner_id = resolveOwner(call, usage)->id;
        if (!ownerPrompt.empty()) {
            const auto ownerLkp = resolveUser(ownerPrompt, "vault create");
            if (!ownerLkp || !ownerLkp.ptr) return invalid(ownerLkp.error);
            req.owner_id = ownerLkp.ptr->id;
        }

        if (req.type == vh::vault::model::VaultType::Local) {
            auto conflictStr = io->prompt(
                "Enter on-sync-conflict policy (overwrite/keep_both/ask) [overwrite] --help for details:", "overwrite");
            while (isHelp(conflictStr)) {
                io->print(LOCAL_CONFLICT_POLICY_HELP);
                conflictStr = io->prompt("Enter on-sync-conflict policy (overwrite/keep_both/ask) [overwrite]:", "overwrite");
            }
            req.sync.conflict_policy = conflictStr;
        } else {
            ops::vaults::S3Spec s3;
            const auto apiKeyStr = io->prompt("Enter API key name or ID (required):");
            if (apiKeyStr.empty()) return invalid("vault create: API key is required for S3 vaults");
            const auto key = parseUInt(apiKeyStr) ? db::query::vault::APIKey::getAPIKey(*parseUInt(apiKeyStr))
                                                  : db::query::vault::APIKey::getAPIKey(apiKeyStr);
            if (!key) return invalid("vault create: API key not found: " + apiKeyStr);
            s3.api_key_id = key->id;

            s3.bucket = io->prompt("Enter S3 bucket name (required):");
            if (s3.bucket.empty()) return invalid("vault create: S3 bucket name is required");
            if (const auto tier = io->prompt("Storage tier [provider default]:", ""); !tier.empty()) s3.storage_tier = tier;

            // Offered defaults are the operator's vaults.s3.* settings (the same ones a non-interactive create gets).
            const auto& remoteDefaults = config::Registry::get().vaults.s3;
            const auto& strategyDefault = remoteDefaults.default_remote_sync_strategy;
            const auto& conflictDefault = remoteDefaults.default_remote_conflict_policy;
            auto strategyStr = io->prompt(
                "Enter sync strategy (cache/sync/mirror) [" + strategyDefault + "] --help for details:", strategyDefault);
            while (isHelp(strategyStr)) {
                io->print(SYNC_STRATEGY_HELP);
                strategyStr = io->prompt("Enter sync strategy (cache/sync/mirror) [" + strategyDefault + "]:", strategyDefault);
            }
            req.sync.strategy = strategyStr;

            auto conflictStr = io->prompt(
                "Enter on-sync-conflict policy (keep_local/keep_remote/keep_newest/ask) [" + conflictDefault +
                "] --help for details:", conflictDefault);
            while (isHelp(conflictStr)) {
                io->print(REMOTE_CONFLICT_POLICY_HELP);
                conflictStr = io->prompt(
                    "Enter on-sync-conflict policy (keep_local/keep_remote/keep_newest/ask) [" + conflictDefault + "]:",
                    conflictDefault);
            }
            req.sync.conflict_policy = conflictStr;
            s3.encrypt_upstream = io->confirm("Enable upstream encryption? (yes/no) [yes]", false);
            req.s3 = s3;
        }

        auto interval = io->prompt("Enter sync interval (e.g. 30s, 10m, 1h) [15m] --help for details:", "15m");
        while (isHelp(interval)) {
            io->print(SYNC_INTERVAL_HELP);
            interval = io->prompt("Enter sync interval (e.g. 30s, 10m, 1h) [15m]:", "15m");
        }
        req.sync.interval = db::encoding::parseSyncInterval(interval);

        return runVaultChange(call, "vault create", [&](const bool accept) {
            auto attempt = req;
            attempt.accept_waiver = accept;
            return ops::vaults::create(call.user, attempt);
        }, createdVaultMessage);
    }

    CommandResult handle_vault_create(const CommandCall& call) {
        if (hasFlag(call, "interactive")) return handle_vault_create_interactive(call);

        const auto usage = resolveUsage({"vault", "create"});
        validatePositionals(call, usage);

        ops::vaults::Create req;
        try {
            req.type = *parseVaultType(call);
            req.name = call.positionals[0];
            req.owner_id = resolveOwner(call, usage)->id;
            req.description = optVal(call, usage->resolveOptional("description")->option_tokens).value_or("");
            req.quota = quotaFromOption(call).value_or(0);
            req.sync = syncPatchFromOptions(call);
            if (req.type == vh::vault::model::VaultType::S3) {
                const auto keyId = apiKeyIdFromOption(call);
                if (!keyId) return invalid("vault create: --api-key is required for S3 vaults");
                const auto bucket = optVal(call, "bucket");
                if (!bucket || bucket->empty()) return invalid("vault create: --bucket is required for S3 vaults");
                req.s3 = ops::vaults::S3Spec{.api_key_id = *keyId, .bucket = *bucket,
                                             .storage_tier = optVal(call, std::vector<std::string>{"storage-tier", "storage-class"}),
                                             .encrypt_upstream = encryptFromFlags(call)};
            }
        } catch (const ops::Error& e) {
            return invalid("vault create: " + std::string(e.what()));
        }

        return runVaultChange(call, "vault create", [&](const bool accept) {
            auto attempt = req;
            attempt.accept_waiver = accept;
            return ops::vaults::create(call.user, attempt);
        }, createdVaultMessage);
    }
}
