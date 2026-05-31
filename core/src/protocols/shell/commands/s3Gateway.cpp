#include "protocols/shell/commands/all.hpp"

#include "config/Registry.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "protocols/s3/CredentialManager.hpp"
#include "protocols/s3/GatewayService.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "runtime/Manager.hpp"
#include "storage/Manager.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "usage/include/UsageManager.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace vh::protocols::shell::commands {

namespace {

std::string yesNo(const bool value) {
    return value ? "yes" : "no";
}

bool isGatewayMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"s3-gateway", cmd}, input);
}

std::shared_ptr<identities::User> resolveTargetUser(const CommandCall& call) {
    const auto userOpt = optVal(call, "user");
    if (!userOpt || userOpt->empty()) return call.user;
    auto target = resolveUser(*userOpt, "s3-gateway");
    if (!target) throw std::runtime_error(target.error);
    if (!call.user->isAdmin() && target.ptr->id != call.user->id)
        throw std::runtime_error("s3-gateway: only admins can target another user");
    return target.ptr;
}

std::shared_ptr<::vh::vault::model::Vault> resolveVaultArg(const CommandCall& call, const std::string& value) {
    if (const auto id = parseUInt(value)) {
        auto vault = db::query::vault::Vault::getVault(*id);
        if (!vault) throw std::runtime_error("s3-gateway bucket bind: vault not found: " + value);
        return vault;
    }

    std::shared_ptr<identities::User> owner = call.user;
    if (const auto ownerOpt = optVal(call, "owner")) {
        auto lookup = resolveUser(*ownerOpt, "s3-gateway bucket bind");
        if (!lookup) throw std::runtime_error(lookup.error);
        owner = lookup.ptr;
    }

    auto vault = db::query::vault::Vault::getVault(value, owner->id);
    if (!vault)
        throw std::runtime_error("s3-gateway bucket bind: vault named '" + value + "' not found for owner " +
                                 std::to_string(owner->id));
    return vault;
}

std::shared_ptr<::vh::vault::model::APIKey> resolveUpstreamApiKey(const std::string& value) {
    if (const auto id = parseUInt(value)) return db::query::vault::APIKey::getAPIKey(*id);
    return db::query::vault::APIKey::getAPIKey(value);
}

void requireVaultOwnerOrAdmin(const CommandCall& call, const std::shared_ptr<::vh::vault::model::Vault>& vault) {
    if (!vault) throw std::runtime_error("s3-gateway: vault not found");
    if (!call.user->isAdmin() && vault->owner_id != call.user->id)
        throw std::runtime_error("s3-gateway: you do not have permission to manage this vault binding");
}

std::string modeOrDefault(const CommandCall& call, std::string fallback = "local") {
    auto mode = optVal(call, "mode").value_or(std::move(fallback));
    std::ranges::transform(mode, mode.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (mode != "local" && mode != "remote_cache" && mode != "remote_proxy")
        throw std::runtime_error("s3-gateway: mode must be local, remote_cache, or remote_proxy");
    return mode;
}

CommandResult saveConfigAndRestart(config::Config cfg, const std::string& message) {
    try {
        cfg.save();
        config::Registry::set(cfg);
        runtime::Manager::instance().restartService("S3GatewayService");
        return ok(message);
    } catch (const std::exception& e) {
        return invalid("s3-gateway config update failed: " + std::string(e.what()));
    }
}

CommandResult handleS3GatewayStatus(const CommandCall&) {
    const auto service = runtime::Manager::instance().getS3GatewayService();
    const auto status = service ? service->gatewayStatus() : protocols::s3::GatewayService::RuntimeStatus{};

    std::ostringstream out;
    out << "s3 gateway\n";
    out << "  running: " << yesNo(status.running) << "\n";
    out << "  configured: " << yesNo(status.configured) << "\n";
    out << "  ready: " << yesNo(status.ready) << "\n";
    out << "  endpoint: " << status.host << ":" << status.port << "\n";
    out << "  active sessions: " << status.activeSessions << "\n";
    out << "  total requests: " << status.totalRequests << "\n";
    out << "  failed requests: " << status.failedRequests << "\n";
    return ok(out.str());
}

CommandResult handleEnable(const CommandCall&) {
    auto cfg = config::Registry::get();
    cfg.s3_gateway.enabled = true;
    return saveConfigAndRestart(cfg, "S3 gateway enabled.\n");
}

CommandResult handleDisable(const CommandCall&) {
    auto cfg = config::Registry::get();
    cfg.s3_gateway.enabled = false;
    return saveConfigAndRestart(cfg, "S3 gateway disabled.\n");
}

CommandResult handleCredsCreate(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto user = resolveTargetUser(call);
    const protocols::s3::CredentialManager manager;
    const auto secret = manager.createCredential(user->id, call.positionals[0]);

    if (hasFlag(call, "json")) {
        nlohmann::json j = {
            {"user_id", user->id},
            {"name", call.positionals[0]},
            {"access_key", secret.credential.access_key},
            {"secret_access_key", secret.secret_access_key}
        };
        return ok(j.dump(4) + "\n");
    }

    std::ostringstream out;
    out << "created S3 gateway credential\n";
    out << "  user: " << user->name << " (" << user->id << ")\n";
    out << "  name: " << call.positionals[0] << "\n";
    out << "  access key: " << secret.credential.access_key << "\n";
    out << "  secret access key: " << secret.secret_access_key << "\n";
    out << "store the secret now; it cannot be listed again.\n";
    return ok(out.str());
}

CommandResult handleCredsList(const CommandCall& call) {
    const auto user = resolveTargetUser(call);
    const auto creds = db::query::s3::Gateway::listCredentials(user->id);

    if (hasFlag(call, "json")) {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& credential : creds) {
            rows.push_back({
                {"id", credential.id},
                {"user_id", credential.user_id},
                {"name", credential.name},
                {"access_key", credential.access_key},
                {"enabled", credential.enabled},
                {"created_at", credential.created_at},
                {"last_used_at", credential.last_used_at ? nlohmann::json(*credential.last_used_at) : nlohmann::json(nullptr)}
            });
        }
        return ok(rows.dump(4) + "\n");
    }

    std::ostringstream out;
    out << "s3 gateway credentials for " << user->name << " (" << user->id << ")\n";
    if (creds.empty()) out << "none\n";
    for (const auto& credential : creds) {
        out << "- " << credential.name << " " << credential.access_key
            << " enabled=" << yesNo(credential.enabled);
        if (credential.last_used_at) out << " last_used=" << *credential.last_used_at;
        out << "\n";
    }
    return ok(out.str());
}

CommandResult handleCredsRevoke(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto value = call.positionals[0];
    bool removed = false;

    if (value.starts_with("VH")) {
        removed = db::query::s3::Gateway::deleteCredentialByAccessKey(value);
    } else {
        const auto user = resolveTargetUser(call);
        removed = db::query::s3::Gateway::deleteCredentialByName(user->id, value);
    }

    if (!removed) return invalid("s3-gateway creds revoke: credential not found");
    return ok("S3 gateway credential revoked.\n");
}

CommandResult handleCreds(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "creds", "create"}, sub)) return handleCredsCreate(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "list"}, sub)) return handleCredsList(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "revoke"}, sub)) return handleCredsRevoke(subcall);
    return invalid(call.constructFullArgs(), "Unknown s3-gateway creds subcommand: '" + std::string(sub) + "'");
}

CommandResult handleBucketList(const CommandCall& call) {
    const auto buckets = protocols::s3::ObjectStore{}.listBuckets(call.user);
    if (hasFlag(call, "json")) {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& bucket : buckets) {
            rows.push_back({
                {"bucket", bucket.bucket_name},
                {"vault_id", bucket.vault_id},
                {"mode", bucket.mode},
                {"api_exclusive", bucket.api_exclusive},
                {"created_by", bucket.created_by ? nlohmann::json(*bucket.created_by) : nlohmann::json(nullptr)},
                {"created_at", bucket.created_at},
                {"updated_at", bucket.updated_at}
            });
        }
        return ok(rows.dump(4) + "\n");
    }

    std::ostringstream out;
    out << "s3 gateway buckets\n";
    if (buckets.empty()) out << "none\n";
    for (const auto& bucket : buckets) {
        out << "- " << bucket.bucket_name << " vault=" << bucket.vault_id
            << " mode=" << bucket.mode
            << " api_exclusive=" << yesNo(bucket.api_exclusive) << "\n";
    }
    return ok(out.str());
}

CommandResult handleBucketBind(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto vaultOpt = optVal(call, "vault");
    if (!vaultOpt || vaultOpt->empty()) return invalid("s3-gateway bucket bind: --vault is required");

    const auto vault = resolveVaultArg(call, *vaultOpt);
    requireVaultOwnerOrAdmin(call, vault);
    const auto mode = modeOrDefault(call, vault->type == ::vh::vault::model::VaultType::S3 ? "remote_cache" : "local");
    db::query::s3::Gateway::bindBucket({
        .vault_id = vault->id,
        .bucket_name = call.positionals[0],
        .api_exclusive = hasFlag(call, "api-exclusive"),
        .mode = mode,
        .created_by = call.user->id
    });
    return ok("Bound bucket " + call.positionals[0] + " to vault " + std::to_string(vault->id) + ".\n");
}

CommandResult handleBucketUnbind(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    if (!db::query::s3::Gateway::unbindBucket(call.positionals[0]))
        return invalid("s3-gateway bucket unbind: bucket binding not found");
    return ok("Unbound bucket " + call.positionals[0] + ".\n");
}

CommandResult handleBucketCreateLocal(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    auto owner = resolveTargetUser(call);
    uintmax_t quota = 0;
    if (const auto quotaOpt = optVal(call, "quota"); quotaOpt && !quotaOpt->empty()) {
        ::vh::vault::model::Vault quotaParser;
        quotaParser.setQuotaFromStr(*quotaOpt);
        quota = quotaParser.quota;
    }
    protocols::s3::ObjectStore store;
    const auto bucket = store.createBucket(call.positionals[0], owner, "local", quota);
    return ok("Created local S3 gateway bucket " + bucket.bucket_name + " on vault " + std::to_string(bucket.vault_id) + ".\n");
}

CommandResult handleBucketCreateRemoteCache(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    if (!call.user->isAdmin())
        return invalid("s3-gateway bucket create-remote-cache: admin permission is required");

    const auto apiKeyOpt = optVal(call, "api-key");
    const auto upstreamBucketOpt = optVal(call, "upstream-bucket");
    if (!apiKeyOpt || apiKeyOpt->empty()) return invalid("s3-gateway bucket create-remote-cache: --api-key is required");
    if (!upstreamBucketOpt || upstreamBucketOpt->empty())
        return invalid("s3-gateway bucket create-remote-cache: --upstream-bucket is required");
    if (hasFlag(call, "encrypt") && hasFlag(call, "no-encrypt"))
        return invalid("s3-gateway bucket create-remote-cache: --encrypt and --no-encrypt are mutually exclusive");

    const auto apiKey = resolveUpstreamApiKey(*apiKeyOpt);
    if (!apiKey) return invalid("s3-gateway bucket create-remote-cache: upstream API key not found");

    if (db::query::s3::Gateway::resolveBucket(call.positionals[0]))
        return invalid("s3-gateway bucket create-remote-cache: gateway bucket already exists");
    if (db::query::vault::Vault::vaultExists(call.positionals[0], call.user->id))
        return invalid("s3-gateway bucket create-remote-cache: vault name already exists for this user");

    auto vault = std::make_shared<::vh::vault::model::S3Vault>();
    vault->name = call.positionals[0];
    vault->description = "S3 gateway remote-cache bucket " + call.positionals[0];
    vault->owner_id = call.user->id;
    vault->type = ::vh::vault::model::VaultType::S3;
    vault->is_active = true;
    vault->api_key_id = apiKey->id;
    vault->bucket = *upstreamBucketOpt;
    vault->encrypt_upstream = !hasFlag(call, "no-encrypt");

    auto policy = std::make_shared<sync::model::RemotePolicy>();
    policy->strategy = sync::model::RemotePolicy::Strategy::Cache;
    policy->conflict_policy = sync::model::RemotePolicy::ConflictPolicy::KeepLocal;
    policy->s3_request_budget = sync::model::s3RequestBudgetForPreset(sync::model::S3BudgetPreset::Balanced);
    policy->max_remote_index_age = std::chrono::hours(24);

    try {
        auto created = runtime::Deps::get().storageManager->addVault(vault, policy);
        db::query::s3::Gateway::bindBucket({
            .vault_id = created->id,
            .bucket_name = call.positionals[0],
            .api_exclusive = true,
            .mode = "remote_cache",
            .created_by = call.user->id
        });
        return ok("Created remote-cache S3 gateway bucket " + call.positionals[0] + " on vault " +
                  std::to_string(created->id) + ".\n");
    } catch (const std::exception& e) {
        return invalid("s3-gateway bucket create-remote-cache: " + std::string(e.what()));
    }
}

CommandResult handleBucket(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "bucket", "list"}, sub)) return handleBucketList(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "bind"}, sub)) return handleBucketBind(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "unbind"}, sub)) return handleBucketUnbind(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "create-local"}, sub)) return handleBucketCreateLocal(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "create-remote-cache"}, sub)) return handleBucketCreateRemoteCache(subcall);
    return invalid(call.constructFullArgs(), "Unknown s3-gateway bucket subcommand: '" + std::string(sub) + "'");
}

CommandResult handleS3Gateway(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    try {
        const auto [sub, subcall] = descend(call);
        if (isGatewayMatch("status", sub)) return handleS3GatewayStatus(subcall);
        if (isGatewayMatch("enable", sub)) return handleEnable(subcall);
        if (isGatewayMatch("disable", sub)) return handleDisable(subcall);
        if (isGatewayMatch("creds", sub)) return handleCreds(subcall);
        if (isGatewayMatch("bucket", sub)) return handleBucket(subcall);
        return invalid(call.constructFullArgs(), "Unknown s3-gateway subcommand: '" + std::string(sub) + "'");
    } catch (const std::exception& e) {
        return invalid(e.what());
    }
}

} // namespace

void registerS3GatewayCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("s3-gateway"), handleS3Gateway);
}

} // namespace vh::protocols::shell::commands
