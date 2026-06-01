#include "protocols/ws/handler/S3Gateway.hpp"

#include "config/Registry.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "protocols/s3/CredentialManager.hpp"
#include "protocols/s3/GatewayService.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/ws/Session.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "runtime/Deps.hpp"
#include "runtime/Manager.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace vh::protocols::ws::handler {
namespace {

using vh::storage::s3::pricing::PriceBudgetMode;
using vh::storage::s3::pricing::PriceBudgetPolicy;
using vh::storage::s3::pricing::PriceBudgetScope;
using vh::storage::s3::pricing::PriceBudgetService;

std::optional<std::string> optionalString(const json& payload, const char* key) {
    if (!payload.is_object() || !payload.contains(key) || payload.at(key).is_null()) return std::nullopt;
    const auto value = payload.at(key).get<std::string>();
    return value.empty() ? std::optional<std::string>{} : std::make_optional(value);
}

std::optional<std::uint32_t> optionalUInt(const json& payload, const char* key) {
    if (!payload.is_object() || !payload.contains(key) || payload.at(key).is_null()) return std::nullopt;
    return payload.at(key).get<std::uint32_t>();
}

std::uint32_t gwLimitFromPayload(const json& payload, const std::uint32_t fallback = 50) {
    if (!payload.is_object()) return fallback;
    return std::clamp<std::uint32_t>(payload.value("limit", fallback), 1, 500);
}

void requireAdmin(const std::shared_ptr<Session>& session, const char* message) {
    if (!session || !session->user || !session->user->isAdmin()) throw std::runtime_error(message);
}

bool canEditVault(const std::shared_ptr<Session>& session, const std::uint32_t vaultId) {
    if (!session || !session->user) return false;
    if (session->user->isSuperAdmin()) return true;
    try {
        if (db::query::vault::Vault::getVaultOwnerId(vaultId) == session->user->id) return true;
    } catch (const std::exception&) {
        return false;
    }
    if (!runtime::Deps::get().storageManager) return false;
    using Perm = vh::rbac::permission::admin::VaultPermissions;
    return vh::rbac::resolver::Admin::has<Perm>({
        .user = session->user,
        .permission = Perm::Edit,
        .vault_id = vaultId
    });
}

bool canViewVault(const std::shared_ptr<Session>& session, const std::uint32_t vaultId) {
    if (!session || !session->user) return false;
    if (session->user->isSuperAdmin()) return true;
    try {
        if (db::query::vault::Vault::getVaultOwnerId(vaultId) == session->user->id) return true;
    } catch (const std::exception&) {
        return false;
    }
    if (!runtime::Deps::get().storageManager) return false;
    using Perm = vh::rbac::permission::admin::VaultPermissions;
    return vh::rbac::resolver::Admin::has<Perm>({
        .user = session->user,
        .permissions = {Perm::View, Perm::ViewStats},
        .vault_id = vaultId
    });
}

void requireVaultEdit(const std::shared_ptr<Session>& session, const std::uint32_t vaultId) {
    if (!canEditVault(session, vaultId))
        throw std::runtime_error("You do not have permission to manage this S3 gateway bucket.");
}

bool ownsCredential(const std::shared_ptr<Session>& session, const std::uint32_t credentialId) {
    const auto owned = db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
    return std::ranges::any_of(owned, [&](const auto& credential) {
        return credential.id == credentialId;
    });
}

json credentialJson(const db::query::s3::GatewayCredential& credential) {
    return {
        {"id", credential.id},
        {"user_id", credential.user_id},
        {"principal_user_id", credential.principal_user_id},
        {"created_by", credential.created_by ? json(*credential.created_by) : json(nullptr)},
        {"name", credential.name},
        {"access_key", credential.access_key},
        {"enabled", credential.enabled},
        {"scope_mode", credential.scope_mode},
        {"enforce_budget_for_local_requests", credential.enforce_budget_for_local_requests},
        {"description", credential.description ? json(*credential.description) : json(nullptr)},
        {"created_at", credential.created_at},
        {"last_used_at", credential.last_used_at ? json(*credential.last_used_at) : json(nullptr)},
        {"expires_at", credential.expires_at ? json(*credential.expires_at) : json(nullptr)}
    };
}

json scopeJson(const db::query::s3::CredentialVaultScope& scope) {
    return {
        {"credential_id", scope.credential_id},
        {"vault_id", scope.vault_id},
        {"can_list", scope.can_list},
        {"can_read", scope.can_read},
        {"can_write", scope.can_write},
        {"can_delete", scope.can_delete},
        {"can_admin", scope.can_admin}
    };
}

json bucketJson(const db::query::s3::BucketBinding& bucket) {
    return {
        {"bucket_name", bucket.bucket_name},
        {"bucket", bucket.bucket_name},
        {"vault_id", bucket.vault_id},
        {"mode", bucket.mode},
        {"api_exclusive", bucket.api_exclusive},
        {"created_by", bucket.created_by ? json(*bucket.created_by) : json(nullptr)},
        {"created_at", bucket.created_at},
        {"updated_at", bucket.updated_at}
    };
}

std::optional<db::query::s3::GatewayCredential> findCredential(
    const std::shared_ptr<Session>& session,
    const std::string& accessKeyOrName,
    const bool includeDisabled = true) {
    auto credentials = session->user->isAdmin()
        ? db::query::s3::Gateway::listCredentialsAdmin(true)
        : db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
    for (const auto& credential : credentials) {
        if (!includeDisabled && !credential.enabled) continue;
        if (credential.access_key == accessKeyOrName || credential.name == accessKeyOrName || std::to_string(credential.id) == accessKeyOrName)
            return credential;
    }
    return std::nullopt;
}

std::vector<db::query::s3::CredentialVaultScope> scopesFromPayload(const json& payload, const uint32_t credentialId) {
    std::vector<db::query::s3::CredentialVaultScope> scopes;
    if (!payload.contains("vault_scopes") || !payload.at("vault_scopes").is_array()) return scopes;
    for (const auto& item : payload.at("vault_scopes")) {
        db::query::s3::CredentialVaultScope scope;
        scope.credential_id = credentialId;
        scope.vault_id = item.at("vault_id").get<std::uint32_t>();
        scope.can_list = item.value("can_list", true);
        scope.can_read = item.value("can_read", true);
        scope.can_write = item.value("can_write", false);
        scope.can_delete = item.value("can_delete", false);
        scope.can_admin = item.value("can_admin", false);
        scopes.push_back(scope);
    }
    return scopes;
}

PriceBudgetPolicy budgetPolicyFromPayload(const json& payload) {
    PriceBudgetPolicy policy;
    policy.scope = vh::storage::s3::pricing::priceBudgetScopeFromString(payload.at("scope").get<std::string>());
    policy.provider_key = optionalString(payload, "provider_key");
    policy.vault_id = optionalUInt(payload, "vault_id");
    policy.gateway_credential_id = optionalUInt(payload, "gateway_credential_id");
    policy.mode = vh::storage::s3::pricing::priceBudgetModeFromString(payload.value("mode", "report"));
    policy.currency = payload.value("currency", "USD");
    policy.max_run_cost = optionalString(payload, "max_run_cost");
    policy.max_daily_cost = optionalString(payload, "max_daily_cost");
    policy.max_monthly_cost = optionalString(payload, "max_monthly_cost");
    policy.require_verified_catalog = payload.value("require_verified_catalog", true);
    policy.allow_stale_catalog = payload.value("allow_stale_catalog", false);
    if (const auto maxAge = optionalUInt(payload, "max_catalog_age_seconds"))
        policy.max_catalog_age_seconds = static_cast<std::int64_t>(*maxAge);
    else
        policy.max_catalog_age_seconds = std::nullopt;
    return policy;
}

bool isGatewayBudgetScope(const PriceBudgetScope scope) {
    return scope == PriceBudgetScope::GatewayCredential ||
        scope == PriceBudgetScope::GatewayCredentialVault;
}

void requireGatewayBudgetScope(const PriceBudgetScope scope) {
    if (!isGatewayBudgetScope(scope))
        throw std::runtime_error("S3 gateway budget endpoints only manage gateway credential budget policies.");
}

void requireCanManagePolicy(const std::shared_ptr<Session>& session, const PriceBudgetPolicy& policy) {
    requireGatewayBudgetScope(policy.scope);
    if (session->user->isAdmin()) return;
    if (policy.scope == PriceBudgetScope::GatewayCredentialVault) {
        if (policy.vault_id && canEditVault(session, *policy.vault_id)) return;
    }
    throw std::runtime_error("You do not have permission to manage this S3 gateway budget policy.");
}

bool canViewPolicy(const std::shared_ptr<Session>& session, const PriceBudgetPolicy& policy) {
    if (session->user->isAdmin()) return true;
    if (policy.scope == PriceBudgetScope::Global || policy.scope == PriceBudgetScope::Provider)
        return false;
    const auto credentialVisible = policy.gateway_credential_id && ownsCredential(session, *policy.gateway_credential_id);
    const auto vaultVisible = policy.vault_id && canViewVault(session, *policy.vault_id);
    return credentialVisible || vaultVisible;
}

void filterGatewayLedger(std::vector<vh::storage::s3::pricing::PriceBudgetLedgerEntry>& ledger) {
    std::erase_if(ledger, [](const auto& entry) {
        return !entry.gateway_credential_id;
    });
}

void filterGatewayTrends(std::vector<vh::storage::s3::pricing::PriceBudgetTrendStats>& trends) {
    std::erase_if(trends, [](const auto& trend) {
        return trend.scope != "gateway_credential" &&
            trend.scope != "gateway_credential_vault";
    });
}

void requireBudgetVisibility(
    const std::shared_ptr<Session>& session,
    const std::optional<std::uint32_t>& vaultId,
    const std::optional<std::uint32_t>& credentialId) {
    if (session->user->isAdmin()) return;
    if (vaultId && canViewVault(session, *vaultId)) return;
    if (credentialId && ownsCredential(session, *credentialId)) return;
    throw std::runtime_error("S3 gateway budget views must be scoped to a vault you can view or a credential you own.");
}

std::shared_ptr<::vh::vault::model::Vault> resolveVault(const json& payload) {
    const auto vaultId = optionalUInt(payload, "vault_id");
    if (!vaultId) throw std::runtime_error("vault_id is required");
    auto vault = db::query::vault::Vault::getVault(*vaultId);
    if (!vault) throw std::runtime_error("vault not found");
    return vault;
}

std::shared_ptr<::vh::vault::model::APIKey> resolveApiKey(const json& payload) {
    if (const auto apiKeyId = optionalUInt(payload, "api_key_id")) return db::query::vault::APIKey::getAPIKey(*apiKeyId);
    if (const auto apiKey = optionalString(payload, "api_key")) return db::query::vault::APIKey::getAPIKey(*apiKey);
    return nullptr;
}

std::string normalizeBucketMode(std::string mode) {
    std::ranges::transform(mode, mode.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (mode != "local" && mode != "remote_cache" && mode != "remote_proxy")
        throw std::runtime_error("S3 gateway bucket mode must be local, remote_cache, or remote_proxy.");
    return mode;
}

std::string normalizeScopeMode(std::string mode) {
    std::ranges::transform(mode, mode.begin(), [](const unsigned char c) {
        return c == '-' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (mode == "user_access" || mode == "global" || mode == "vault_allowlist")
        return mode;
    throw std::runtime_error("S3 gateway credential scope must be user-access, global, or vault-allowlist.");
}

std::string bucketModeForVault(const json& payload, const std::shared_ptr<::vh::vault::model::Vault>& vault) {
    if (!vault) throw std::runtime_error("vault not found");
    const bool s3Backed = vault->type == ::vh::vault::model::VaultType::S3;
    auto mode = normalizeBucketMode(payload.value("mode", s3Backed ? "remote_cache" : "local"));
    if (s3Backed && mode == "local")
        throw std::runtime_error("S3/R2 vaults must use remote_cache or remote_proxy mode.");
    if (!s3Backed && mode != "local")
        throw std::runtime_error("Local vaults can only use local S3 gateway bucket mode.");
    return mode;
}

} // namespace

json S3Gateway::status(const json&, const std::shared_ptr<Session>& session) {
    requireAdmin(session, "Admin permission is required to view S3 gateway status.");
    const auto service = runtime::Manager::instance().getS3GatewayService();
    const auto status = service ? service->gatewayStatus() : protocols::s3::GatewayService::RuntimeStatus{};
    return {{"status", {
        {"running", status.running},
        {"configured", status.configured},
        {"ready", status.ready},
        {"host", status.host},
        {"port", status.port},
        {"endpoint", status.host + ":" + std::to_string(status.port)},
        {"active_sessions", status.activeSessions},
        {"total_requests", status.totalRequests},
        {"failed_requests", status.failedRequests}
    }}};
}

json S3Gateway::credentialsCreate(const json& payload, const std::shared_ptr<Session>& session) {
    auto principalId = session->user->id;
    if (const auto payloadPrincipalId = optionalUInt(payload, "principal_user_id"))
        principalId = *payloadPrincipalId;
    if (const auto payloadUserId = optionalUInt(payload, "user_id"))
        principalId = *payloadUserId;
    if (!session->user->isAdmin() && principalId != session->user->id)
        throw std::runtime_error("Only admins may create S3 gateway credentials for another user.");

    protocols::s3::CredentialCreateOptions options;
    options.created_by = session->user->id;
    options.principal_user_id = principalId;
    options.name = payload.at("name").get<std::string>();
    options.scope_mode = normalizeScopeMode(payload.value("scope_mode", payload.value("scope", "user_access")));
    options.description = optionalString(payload, "description");
    if (const auto expiresAt = optionalUInt(payload, "expires_at"))
        options.expires_at = static_cast<std::time_t>(*expiresAt);
    options.vault_scopes = scopesFromPayload(payload, 0);
    options.enforce_budget_for_local_requests = payload.value("enforce_budget_for_local_requests", false);

    const protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential(options);
    return {
        {"credential", credentialJson(secret.credential)},
        {"secret_access_key", secret.secret_access_key}
    };
}

json S3Gateway::credentialsList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto includeDisabled = payload.is_object()
        ? payload.value("include_disabled", true)
        : true;
    const auto credentials = session->user->isAdmin()
        ? db::query::s3::Gateway::listCredentialsAdmin(includeDisabled)
        : db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
    json rows = json::array();
    for (const auto& credential : credentials) {
        if (!includeDisabled && !credential.enabled) continue;
        rows.push_back(credentialJson(credential));
    }
    return {{"credentials", rows}};
}

json S3Gateway::credentialsRevoke(const json& payload, const std::shared_ptr<Session>& session) {
    const auto value = payload.value("access_key", payload.value("name", std::string{}));
    if (value.empty()) throw std::runtime_error("access_key or name is required");
    const auto credential = findCredential(session, value);
    if (!credential) throw std::runtime_error("S3 gateway credential not found");
    if (!session->user->isAdmin() && credential->principal_user_id != session->user->id)
        throw std::runtime_error("You do not have permission to revoke this S3 gateway credential.");
    return {{"revoked", db::query::s3::Gateway::deleteCredentialByAccessKey(credential->access_key)}};
}

json S3Gateway::credentialsScopeUpdate(const json& payload, const std::shared_ptr<Session>& session) {
    const auto value = payload.value("access_key", payload.value("name", std::string{}));
    const auto credential = findCredential(session, value);
    if (!credential) throw std::runtime_error("S3 gateway credential not found");
    if (!session->user->isAdmin() && credential->principal_user_id != session->user->id)
        throw std::runtime_error("You do not have permission to update this S3 gateway credential.");
    auto principalId = credential->principal_user_id;
    if (const auto payloadPrincipalId = optionalUInt(payload, "principal_user_id"))
        principalId = *payloadPrincipalId;
    if (!session->user->isAdmin() && principalId != session->user->id)
        throw std::runtime_error("Only admins may retarget S3 gateway credentials.");
    const auto scopeMode = normalizeScopeMode(payload.value("scope_mode", credential->scope_mode));
    if (scopeMode == "global" && !session->user->isAdmin())
        throw std::runtime_error("Global S3 gateway credentials require admin permission.");
    const auto description = payload.contains("description")
        ? optionalString(payload, "description")
        : credential->description;
    auto expiresAt = credential->expires_at;
    if (payload.contains("expires_at")) {
        if (const auto rawExpires = optionalUInt(payload, "expires_at"))
            expiresAt = static_cast<std::time_t>(*rawExpires);
        else
            expiresAt = std::nullopt;
    }
    const auto enforceLocalBudget = payload.contains("enforce_budget_for_local_requests")
        ? std::make_optional(payload.at("enforce_budget_for_local_requests").get<bool>())
        : std::optional<bool>{};

    const auto requestedScopes = payload.contains("vault_scopes")
        ? scopesFromPayload(payload, credential->id)
        : (scopeMode == "vault_allowlist"
            ? db::query::s3::Gateway::listCredentialScopes(credential->id)
            : std::vector<db::query::s3::CredentialVaultScope>{});
    protocols::s3::CredentialManager::validateScopeMutation(
        session->user->id,
        principalId,
        scopeMode,
        requestedScopes);

    db::query::s3::Gateway::updateCredentialScopeMode(
        credential->id,
        scopeMode,
        principalId,
        scopeMode == "global" ? std::make_optional(session->user->id) : credential->created_by,
        description,
        expiresAt,
        enforceLocalBudget);
    if (scopeMode != "vault_allowlist")
        db::query::s3::Gateway::replaceCredentialScopes(credential->id, {});
    else if (payload.contains("vault_scopes"))
        db::query::s3::Gateway::replaceCredentialScopes(credential->id, requestedScopes);

    auto updated = db::query::s3::Gateway::getCredentialByAccessKey(credential->access_key);
    return {{"credential", updated ? credentialJson(*updated) : json(nullptr)}};
}

json S3Gateway::credentialsScopeList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto value = payload.value("access_key", payload.value("name", std::string{}));
    const auto credential = findCredential(session, value);
    if (!credential) throw std::runtime_error("S3 gateway credential not found");
    if (!session->user->isAdmin() && credential->principal_user_id != session->user->id)
        throw std::runtime_error("You do not have permission to view this S3 gateway credential.");
    json rows = json::array();
    for (const auto& scope : db::query::s3::Gateway::listCredentialScopes(credential->id))
        rows.push_back(scopeJson(scope));
    return {{"credential", credentialJson(*credential)}, {"scopes", rows}};
}

json S3Gateway::bucketsList(const json&, const std::shared_ptr<Session>& session) {
    json rows = json::array();
    for (const auto& bucket : protocols::s3::ObjectStore{}.listBuckets(session->user))
        rows.push_back(bucketJson(bucket));
    return {{"buckets", rows}};
}

json S3Gateway::bucketsBind(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vault = resolveVault(payload);
    requireVaultEdit(session, vault->id);
    db::query::s3::Gateway::bindBucket({
        .vault_id = vault->id,
        .bucket_name = payload.at("bucket_name").get<std::string>(),
        .api_exclusive = payload.value("api_exclusive", false),
        .mode = bucketModeForVault(payload, vault),
        .created_by = session->user->id
    });
    return {{"bound", true}};
}

json S3Gateway::bucketsUnbind(const json& payload, const std::shared_ptr<Session>& session) {
    const auto bucketName = payload.at("bucket_name").get<std::string>();
    const auto binding = db::query::s3::Gateway::resolveBucket(bucketName);
    if (!binding) return {{"unbound", false}};
    requireVaultEdit(session, binding->vault_id);
    return {{"unbound", db::query::s3::Gateway::unbindBucket(bucketName)}};
}

json S3Gateway::bucketsCreateLocal(const json& payload, const std::shared_ptr<Session>& session) {
    const auto ownerId = payload.value("owner_id", session->user->id);
    if (!session->user->isAdmin() && ownerId != session->user->id)
        throw std::runtime_error("Only admins may create local gateway buckets for another user.");
    auto owner = db::query::identities::User::getUserById(ownerId);
    if (!owner) throw std::runtime_error("Owner user not found.");
    const auto bucket = protocols::s3::ObjectStore{}.createBucket(
        payload.at("bucket_name").get<std::string>(),
        owner,
        "local",
        payload.value("quota_bytes", static_cast<uintmax_t>(0)));
    return {{"bucket", bucketJson(*db::query::s3::Gateway::resolveBucket(bucket.bucket_name))}};
}

json S3Gateway::bucketsCreateRemoteCache(const json& payload, const std::shared_ptr<Session>& session) {
    requireAdmin(session, "Admin permission is required to create remote-cache S3 gateway buckets.");
    const auto apiKey = resolveApiKey(payload);
    if (!apiKey) throw std::runtime_error("Upstream API key not found.");

    auto vault = std::make_shared<::vh::vault::model::S3Vault>();
    vault->name = payload.at("bucket_name").get<std::string>();
    vault->description = payload.value("description", "S3 gateway remote-cache bucket " + vault->name);
    vault->owner_id = payload.value("owner_id", session->user->id);
    vault->type = ::vh::vault::model::VaultType::S3;
    vault->is_active = true;
    vault->api_key_id = apiKey->id;
    vault->bucket = payload.at("upstream_bucket").get<std::string>();
    vault->encrypt_upstream = payload.value("encrypt_upstream", true);

    auto policy = std::make_shared<sync::model::RemotePolicy>();
    policy->strategy = sync::model::RemotePolicy::Strategy::Cache;
    policy->conflict_policy = sync::model::RemotePolicy::ConflictPolicy::KeepLocal;
    policy->s3_request_budget = sync::model::s3RequestBudgetForPreset(sync::model::S3BudgetPreset::Balanced);
    policy->max_remote_index_age = std::chrono::hours(24);

    auto created = runtime::Deps::get().storageManager->addVault(vault, policy);
    db::query::s3::Gateway::bindBucket({
        .vault_id = created->id,
        .bucket_name = vault->name,
        .api_exclusive = true,
        .mode = "remote_cache",
        .created_by = session->user->id
    });
    return {{"bucket", bucketJson(*db::query::s3::Gateway::resolveBucket(vault->name))}};
}

json S3Gateway::budgetPolicyList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto includeInactive = payload.is_object()
        ? payload.value("include_inactive", true)
        : true;
    const auto policies = PriceBudgetService{}.listPolicies(includeInactive);
    const auto credentialId = optionalUInt(payload, "gateway_credential_id");
    const auto vaultId = optionalUInt(payload, "vault_id");
    json rows = json::array();
    for (const auto& policy : policies) {
        if (!isGatewayBudgetScope(policy.scope)) continue;
        if (credentialId && policy.gateway_credential_id != credentialId) continue;
        if (vaultId) {
            if (policy.scope == PriceBudgetScope::GatewayCredentialVault && policy.vault_id != vaultId) continue;
            if (policy.scope == PriceBudgetScope::GatewayCredential && !credentialId) continue;
            if (policy.scope != PriceBudgetScope::GatewayCredential &&
                policy.scope != PriceBudgetScope::GatewayCredentialVault &&
                policy.vault_id != vaultId)
                continue;
        }
        if (!canViewPolicy(session, policy)) continue;
        rows.push_back(policy);
    }
    return {{"policies", rows}};
}

json S3Gateway::budgetPolicyUpsert(const json& payload, const std::shared_ptr<Session>& session) {
    auto policy = budgetPolicyFromPayload(payload);
    requireGatewayBudgetScope(policy.scope);
    requireCanManagePolicy(session, policy);
    return {{"policy", PriceBudgetService{}.upsertPolicy(std::move(policy))}};
}

json S3Gateway::budgetPolicyDisable(const json& payload, const std::shared_ptr<Session>& session) {
    auto policy = budgetPolicyFromPayload(payload);
    requireGatewayBudgetScope(policy.scope);
    requireCanManagePolicy(session, policy);
    return {{"disabled", PriceBudgetService{}.disablePolicy(
        policy.scope,
        policy.provider_key,
        policy.vault_id,
        policy.gateway_credential_id)}};
}

json S3Gateway::budgetLedgerList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = optionalUInt(payload, "vault_id");
    const auto credentialId = optionalUInt(payload, "gateway_credential_id");
    requireBudgetVisibility(session, vaultId, credentialId);
    auto ledger = PriceBudgetService{}.listLedger(gwLimitFromPayload(payload), vaultId, credentialId);
    filterGatewayLedger(ledger);
    return {{"ledger", ledger}};
}

json S3Gateway::budgetStatus(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = optionalUInt(payload, "vault_id");
    const auto credentialId = optionalUInt(payload, "gateway_credential_id");
    requireBudgetVisibility(session, vaultId, credentialId);
    PriceBudgetService service;
    service.expireStaleReservations();
    auto ledger = service.listLedger(gwLimitFromPayload(payload, 20), vaultId, credentialId);
    filterGatewayLedger(ledger);
    auto trends = service.trendStats(vaultId, credentialId);
    filterGatewayTrends(trends);
    return {
        {"policies", budgetPolicyList(payload, session).at("policies")},
        {"ledger", ledger},
        {"trends", trends}
    };
}

} // namespace vh::protocols::ws::handler
