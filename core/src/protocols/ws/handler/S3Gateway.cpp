#include "protocols/ws/handler/S3Gateway.hpp"

#include "config/Registry.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/Permission.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "protocols/s3/CredentialManager.hpp"
#include "protocols/s3/GatewayService.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/ws/Session.hpp"
#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/permission/admin/S3Gateway.hpp"
#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/permission/vault/Roles.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "rbac/role/Vault.hpp"
#include "runtime/Deps.hpp"
#include "runtime/Manager.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "sync/model/LocalPolicy.hpp"
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

std::shared_ptr<::vh::rbac::role::Vault> resolveVaultRole(const json& payload);

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

bool hasGatewayPermission(
    const std::shared_ptr<Session>& session,
    const vh::rbac::permission::admin::S3GatewayPermissions permission) {
    if (!session || !session->user) return false;
    if (session->user->isSuperAdmin()) return true;
    using Perm = vh::rbac::permission::admin::S3GatewayPermissions;
    return vh::rbac::resolver::Admin::has<Perm>({
        .user = session->user,
        .permission = permission
    });
}

void requireGatewayPermission(
    const std::shared_ptr<Session>& session,
    const vh::rbac::permission::admin::S3GatewayPermissions permission,
    const char* message) {
    if (!hasGatewayPermission(session, permission)) throw std::runtime_error(message);
}

bool canManageGatewayCredentials(const std::shared_ptr<Session>& session) {
    return hasGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials);
}

bool canAssignGatewayPrincipal(const std::shared_ptr<Session>& session) {
    return hasGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::AssignPrincipal);
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

bool canCreateVaultForOwner(const std::shared_ptr<Session>& session, const std::uint32_t ownerId) {
    if (!session || !session->user) return false;
    using Perm = vh::rbac::permission::admin::VaultPermissions;
    return vh::rbac::resolver::Admin::has<Perm>({
        .user = session->user,
        .permission = Perm::Create,
        .target_user_id = ownerId
    });
}

void requireVaultCreateForOwner(const std::shared_ptr<Session>& session, const std::uint32_t ownerId) {
    if (!canCreateVaultForOwner(session, ownerId))
        throw std::runtime_error("You do not have permission to create a gateway bucket for this owner.");
}

bool ownsCredential(const std::shared_ptr<Session>& session, const std::uint32_t credentialId) {
    const auto owned = db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
    return std::ranges::any_of(owned, [&](const auto& credential) {
        return credential.id == credentialId;
    });
}

bool canViewCredential(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential) {
    if (!session || !session->user) return false;
    return credential.principal_user_id == session->user->id || canManageGatewayCredentials(session);
}

bool canManageCredential(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential) {
    return canViewCredential(session, credential);
}

void requireViewCredential(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential) {
    if (!canViewCredential(session, credential))
        throw std::runtime_error("You do not have permission to view this S3 gateway credential.");
}

void requireManageCredential(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential) {
    if (!canManageCredential(session, credential))
        throw std::runtime_error("You do not have permission to manage this S3 gateway credential.");
}

void requireCredentialPrincipalMutable(
    const std::shared_ptr<Session>& session,
    const std::uint32_t principalUserId) {
    if (!session || !session->user) throw std::runtime_error("S3 gateway credential principal update requires a user.");
    if (principalUserId != session->user->id && !canAssignGatewayPrincipal(session))
        throw std::runtime_error("admin.s3_gateway.assign_principal is required to assign another S3 gateway principal.");
}

json credentialJson(const db::query::s3::GatewayCredential& credential) {
    json principalJson = nullptr;
    if (const auto principal = db::query::identities::User::getUserById(credential.principal_user_id)) {
        principalJson = {
            {"id", principal->id},
            {"name", principal->name},
            {"email", principal->email ? json(*principal->email) : json(nullptr)}
        };
    }

    return {
        {"id", credential.id},
        {"user_id", credential.user_id},
        {"principal_user_id", credential.principal_user_id},
        {"principal_user", principalJson},
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

json vaultJson(const std::shared_ptr<::vh::vault::model::Vault>& vault) {
    if (!vault) return nullptr;
    return {
        {"id", vault->id},
        {"name", vault->name},
        {"slug", vault->slug},
        {"fuse_name", vault->fuse_name ? json(*vault->fuse_name) : json(nullptr)},
        {"effective_fuse_name", vault->effectiveFuseName()},
        {"owner_id", vault->owner_id}
    };
}

json roleJson(const std::shared_ptr<::vh::rbac::role::Vault>& role) {
    if (!role) return nullptr;
    json out = *role;
    return out;
}

json assignmentJson(const db::query::s3::CredentialVaultRoleAssignment& assignment) {
    const auto credential = db::query::s3::Gateway::listCredentialsAdmin(true);
    json credentialOut = nullptr;
    for (const auto& item : credential) {
        if (item.id == assignment.credential_id) {
            credentialOut = credentialJson(item);
            break;
        }
    }
    const auto vault = db::query::vault::Vault::getVault(assignment.vault_id);
    const auto role = db::query::rbac::role::Vault::get(assignment.vault_role_id);
    return {
        {"id", assignment.id},
        {"assignment_id", assignment.id},
        {"credential_id", assignment.credential_id},
        {"credential", credentialOut},
        {"vault_id", assignment.vault_id},
        {"vault", vaultJson(vault)},
        {"vault_role_id", assignment.vault_role_id},
        {"role", roleJson(role)},
        {"enabled", assignment.enabled},
        {"created_by", assignment.created_by ? json(*assignment.created_by) : json(nullptr)},
        {"created_at", assignment.created_at},
        {"updated_at", assignment.updated_at}
    };
}

json defaultRoleJson(const db::query::s3::CredentialDefaultVaultRole& defaultRole) {
    const auto credential = db::query::s3::Gateway::listCredentialsAdmin(true);
    json credentialOut = nullptr;
    for (const auto& item : credential) {
        if (item.id == defaultRole.credential_id) {
            credentialOut = credentialJson(item);
            break;
        }
    }
    const auto role = db::query::rbac::role::Vault::get(defaultRole.vault_role_id);
    return {
        {"id", defaultRole.id},
        {"default_role_id", defaultRole.id},
        {"credential_id", defaultRole.credential_id},
        {"credential", credentialOut},
        {"vault_role_id", defaultRole.vault_role_id},
        {"role", roleJson(role)},
        {"enabled", defaultRole.enabled},
        {"created_by", defaultRole.created_by ? json(*defaultRole.created_by) : json(nullptr)},
        {"created_at", defaultRole.created_at},
        {"updated_at", defaultRole.updated_at}
    };
}

json selectedVaultJson(const db::query::s3::CredentialSelectedVault& selectedVault) {
    const auto vault = db::query::vault::Vault::getVault(selectedVault.vault_id);
    return {
        {"credential_id", selectedVault.credential_id},
        {"vault_id", selectedVault.vault_id},
        {"vault", vaultJson(vault)},
        {"enabled", selectedVault.enabled},
        {"created_by", selectedVault.created_by ? json(*selectedVault.created_by) : json(nullptr)},
        {"created_at", selectedVault.created_at},
        {"updated_at", selectedVault.updated_at}
    };
}

json overrideJson(
    const db::query::s3::GatewayCredential& credential,
    const std::shared_ptr<::vh::vault::model::Vault>& vault,
    const ::vh::rbac::permission::Override& overrideRule) {
    return {
        {"id", overrideRule.id},
        {"override_id", overrideRule.id},
        {"assignment_id", overrideRule.assignment_id},
        {"credential_id", credential.id},
        {"credential", credentialJson(credential)},
        {"vault_id", vault ? json(vault->id) : json(nullptr)},
        {"vault", vaultJson(vault)},
        {"permission_id", overrideRule.permission.id},
        {"permission_name", overrideRule.permission.qualified_name},
        {"permission_qualified", overrideRule.permission.qualified_name},
        {"permission", overrideRule.permission},
        {"glob_path", overrideRule.glob_path()},
        {"effect", ::vh::rbac::permission::to_string(overrideRule.effect)},
        {"enabled", overrideRule.enabled}
    };
}

json defaultOverrideJson(
    const db::query::s3::GatewayCredential& credential,
    const ::vh::rbac::permission::Override& overrideRule) {
    auto out = overrideJson(credential, nullptr, overrideRule);
    out["default_role_id"] = overrideRule.assignment_id;
    out["gateway_credential_default_role_id"] = overrideRule.assignment_id;
    return out;
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
    auto credentials = canManageGatewayCredentials(session)
        ? db::query::s3::Gateway::listCredentialsAdmin(true)
        : db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
    for (const auto& credential : credentials) {
        if (!includeDisabled && !credential.enabled) continue;
        if (credential.access_key == accessKeyOrName || credential.name == accessKeyOrName || std::to_string(credential.id) == accessKeyOrName)
            return credential;
    }
    return std::nullopt;
}

std::optional<db::query::s3::GatewayCredential> findCredential(
    const std::shared_ptr<Session>& session,
    const json& payload,
    const bool includeDisabled = true) {
    if (const auto credentialId = optionalUInt(payload, "credential_id")) {
        const auto credentials = canManageGatewayCredentials(session)
            ? db::query::s3::Gateway::listCredentialsAdmin(true)
            : db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
        for (const auto& credential : credentials) {
            if (!includeDisabled && !credential.enabled) continue;
            if (credential.id == *credentialId) return credential;
        }
        return std::nullopt;
    }
    if (const auto id = optionalUInt(payload, "id")) {
        const auto credentials = canManageGatewayCredentials(session)
            ? db::query::s3::Gateway::listCredentialsAdmin(true)
            : db::query::s3::Gateway::listCredentialsForPrincipal(session->user->id);
        for (const auto& credential : credentials) {
            if (!includeDisabled && !credential.enabled) continue;
            if (credential.id == *id) return credential;
        }
        return std::nullopt;
    }
    auto value = payload.value("access_key", payload.value("credential_access_key", std::string{}));
    if (value.empty()) value = payload.value("name", payload.value("credential_name", std::string{}));
    if (value.empty()) value = payload.value("credential", std::string{});
    if (value.empty()) return std::nullopt;
    return findCredential(session, value, includeDisabled);
}

db::query::s3::GatewayCredential requireCredentialFromPayload(
    const std::shared_ptr<Session>& session,
    const json& payload) {
    const auto credential = findCredential(session, payload);
    if (!credential) throw std::runtime_error("S3 gateway credential not found");
    return *credential;
}

std::vector<db::query::s3::CredentialVaultAccessShorthand> scopesFromPayload(const json& payload, const uint32_t credentialId) {
    std::vector<db::query::s3::CredentialVaultAccessShorthand> scopes;
    if (!payload.contains("vault_scopes") || !payload.at("vault_scopes").is_array()) return scopes;
    for (const auto& item : payload.at("vault_scopes")) {
        db::query::s3::CredentialVaultAccessShorthand scope;
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

std::vector<std::uint32_t> selectedVaultIdsFromPayload(const json& payload) {
    std::vector<std::uint32_t> vaultIds;
    const json* source = nullptr;
    if (payload.contains("selected_vault_ids") && payload.at("selected_vault_ids").is_array())
        source = &payload.at("selected_vault_ids");
    else if (payload.contains("vault_ids") && payload.at("vault_ids").is_array())
        source = &payload.at("vault_ids");
    if (!source) return vaultIds;

    for (const auto& item : *source) {
        if (item.is_number_unsigned()) vaultIds.push_back(item.get<std::uint32_t>());
        else if (item.is_object() && item.contains("vault_id")) vaultIds.push_back(item.at("vault_id").get<std::uint32_t>());
        else if (item.is_object() && item.contains("id")) vaultIds.push_back(item.at("id").get<std::uint32_t>());
    }
    return vaultIds;
}

std::optional<std::uint32_t> defaultVaultRoleIdFromPayload(const json& payload) {
    if (const auto id = optionalUInt(payload, "default_vault_role_id")) return id;
    if (const auto id = optionalUInt(payload, "default_role_id")) return id;
    if (payload.contains("vault_role_id") || payload.contains("role_id") ||
        payload.contains("vault_role_name") || payload.contains("role_name") || payload.contains("role")) {
        return resolveVaultRole(payload)->id;
    }
    return std::nullopt;
}

std::vector<std::uint32_t> enabledSelectedVaultIds(const db::query::s3::GatewayCredential& credential) {
    std::vector<std::uint32_t> out;
    for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential.id)) {
        if (selectedVault.enabled) out.push_back(selectedVault.vault_id);
    }
    return out;
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
    requireGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::ManageBudgets,
        "admin.s3_gateway.manage_budgets is required to manage S3 gateway budget policies.");
    if (policy.scope == PriceBudgetScope::GatewayCredentialVault) {
        if (policy.vault_id && canEditVault(session, *policy.vault_id)) return;
        throw std::runtime_error("You do not have permission to manage this S3 gateway key/vault budget policy.");
    }
}

bool canViewPolicy(const std::shared_ptr<Session>& session, const PriceBudgetPolicy& policy) {
    if (hasGatewayPermission(session, vh::rbac::permission::admin::S3GatewayPermissions::View)) return true;
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
    if (hasGatewayPermission(session, vh::rbac::permission::admin::S3GatewayPermissions::View)) return;
    if (vaultId && canViewVault(session, *vaultId)) return;
    if (credentialId && ownsCredential(session, *credentialId)) return;
    throw std::runtime_error("S3 gateway budget views must be scoped to a vault you can view or a credential you own.");
}

std::shared_ptr<::vh::vault::model::Vault> resolveVault(const json& payload) {
    const auto vaultId = optionalUInt(payload, "vault_id");
    if (vaultId) {
        auto vault = db::query::vault::Vault::getVault(*vaultId);
        if (!vault) throw std::runtime_error("vault not found");
        return vault;
    }
    auto vaultName = optionalString(payload, "vault_name");
    if (!vaultName) vaultName = optionalString(payload, "vault");
    if (!vaultName) throw std::runtime_error("vault_id or vault_name is required");
    auto vaults = db::query::vault::Vault::listVaults();
    auto it = std::ranges::find_if(vaults, [&](const auto& vault) {
        return vault && vault->name == *vaultName;
    });
    auto vault = it == vaults.end() ? nullptr : *it;
    if (!vault) throw std::runtime_error("vault not found");
    return vault;
}

std::shared_ptr<::vh::rbac::role::Vault> resolveVaultRole(const json& payload) {
    auto roleId = optionalUInt(payload, "vault_role_id");
    if (!roleId) roleId = optionalUInt(payload, "role_id");
    if (roleId) {
        auto role = db::query::rbac::role::Vault::get(*roleId);
        if (!role) throw std::runtime_error("vault role not found");
        return role;
    }
    auto roleName = optionalString(payload, "vault_role_name");
    if (!roleName) roleName = optionalString(payload, "role_name");
    if (!roleName) roleName = optionalString(payload, "role");
    if (!roleName) throw std::runtime_error("vault_role_id or vault_role_name is required");
    auto role = db::query::rbac::role::Vault::get(*roleName);
    if (!role) throw std::runtime_error("vault role not found");
    return role;
}

std::shared_ptr<::vh::rbac::permission::Permission> resolvePermission(const json& payload) {
    auto permissionId = optionalUInt(payload, "permission_id");
    if (!permissionId) permissionId = optionalUInt(payload, "id");
    if (permissionId) return db::query::rbac::Permission::getPermission(*permissionId);

    auto permissionName = optionalString(payload, "permission_qualified");
    if (!permissionName) permissionName = optionalString(payload, "permission_name");
    if (!permissionName) permissionName = optionalString(payload, "permission");
    if (!permissionName) throw std::runtime_error("permission_id or permission name is required");
    std::vector<std::string> candidates{*permissionName};
    if (permissionName->find('.') == std::string::npos) {
        candidates.push_back("vault.fs.files." + *permissionName);
        candidates.push_back("vault.fs.directories." + *permissionName);
    }
    for (const auto& candidate : candidates) {
        try {
            return db::query::rbac::Permission::getPermissionByName(candidate);
        } catch (const std::exception&) {
        }
    }
    throw std::runtime_error("permission not found: " + *permissionName);
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

void requireCredentialVaultRolePermission(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential,
    const std::uint32_t vaultId,
    const vh::rbac::permission::vault::RolePermissions permission,
    const char* message) {
    if (credential.principal_user_id != session->user->id && !canAssignGatewayPrincipal(session))
        throw std::runtime_error("admin.s3_gateway.assign_principal is required to manage roles for another principal.");
    using Perm = vh::rbac::permission::vault::RolePermissions;
    if (!vh::rbac::resolver::Vault::has<Perm>({
            .user = session->user,
            .permission = permission,
            .target_subject_type = std::string{"user"},
            .target_subject_id = credential.principal_user_id,
            .vault_id = vaultId
        }))
        throw std::runtime_error(message);
}

bool principalCanAccessVault(
    const db::query::s3::GatewayCredential& credential,
    const std::uint32_t vaultId) {
    const auto principal = db::query::identities::User::getUserById(credential.principal_user_id);
    if (!principal || !principal->meta.is_active) return false;
    if (principal->isSuperAdmin()) return true;
    using Perm = vh::rbac::permission::vault::FilesystemAction;
    return vh::rbac::resolver::Vault::has<Perm>({
            .user = principal,
            .permission = Perm::List,
            .vault_id = vaultId,
            .path = "/"
        }) ||
        vh::rbac::resolver::Vault::has<Perm>({
            .user = principal,
            .permission = Perm::Read,
            .vault_id = vaultId,
            .path = "/"
        }) ||
        vh::rbac::resolver::Vault::has<Perm>({
            .user = principal,
            .permission = Perm::Write,
            .vault_id = vaultId,
            .path = "/"
        }) ||
        vh::rbac::resolver::Vault::has<Perm>({
            .user = principal,
            .permission = Perm::Delete,
            .vault_id = vaultId,
            .path = "/"
        });
}

void requireSelectedVaultGrantPermission(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential,
    const std::uint32_t vaultId) {
    requireCredentialVaultRolePermission(
        session,
        credential,
        vaultId,
        vh::rbac::permission::vault::RolePermissions::Assign,
        "You do not have permission to select this vault for the S3 gateway credential principal.");
    if (!principalCanAccessVault(credential, vaultId))
        throw std::runtime_error("The S3 gateway credential principal cannot access this vault.");
}

void requireSelectedVaultGrantPermission(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential,
    const std::vector<std::uint32_t>& vaultIds) {
    for (const auto vaultId : vaultIds)
        requireSelectedVaultGrantPermission(session, credential, vaultId);
}

void requireDefaultOverridePermission(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential,
    const vh::rbac::permission::vault::RolePermissions permission,
    const char* message) {
    if (credential.scope_mode == "global") {
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials,
            "admin.s3_gateway.manage_credentials is required to manage global S3 gateway credential role overrides.");
        return;
    }

    for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential.id)) {
        if (!selectedVault.enabled) continue;
        requireCredentialVaultRolePermission(
            session,
            credential,
            selectedVault.vault_id,
            permission,
            message);
    }
}

std::vector<db::query::s3::CredentialVaultRoleAssignment> visibleAssignmentsFor(
    const std::shared_ptr<Session>& session,
    const db::query::s3::GatewayCredential& credential) {
    requireViewCredential(session, credential);
    auto assignments = db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    std::erase_if(assignments, [&](const auto& assignment) {
        using Perm = vh::rbac::permission::vault::RolePermissions;
        return !vh::rbac::resolver::Vault::has<Perm>({
            .user = session->user,
            .permission = Perm::View,
            .target_subject_type = std::string{"user"},
            .target_subject_id = credential.principal_user_id,
            .vault_id = assignment.vault_id
        });
    });
    return assignments;
}

} // namespace

json S3Gateway::status(const json&, const std::shared_ptr<Session>& session) {
    requireGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::View,
        "admin.s3_gateway.view is required to view S3 gateway status.");
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
    if (principalId != session->user->id)
        requireCredentialPrincipalMutable(session, principalId);

    protocols::s3::CredentialCreateOptions options;
    options.created_by = session->user->id;
    options.principal_user_id = principalId;
    options.name = payload.at("name").get<std::string>();
    options.scope_mode = normalizeScopeMode(payload.value("scope_mode", payload.value("scope", "user_access")));
    options.description = optionalString(payload, "description");
    if (const auto expiresAt = optionalUInt(payload, "expires_at"))
        options.expires_at = static_cast<std::time_t>(*expiresAt);
    options.default_vault_role_id = defaultVaultRoleIdFromPayload(payload);
    options.selected_vault_ids = selectedVaultIdsFromPayload(payload);
    options.vault_scopes = scopesFromPayload(payload, 0);
    options.enforce_budget_for_local_requests = payload.value("enforce_budget_for_local_requests", false);
    if (options.scope_mode == "global")
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials,
            "admin.s3_gateway.manage_credentials is required to create global S3 gateway credentials.");
    if (options.scope_mode == "vault_allowlist")
        requireSelectedVaultGrantPermission(
            session,
            db::query::s3::GatewayCredential{
                .principal_user_id = principalId
            },
            !options.selected_vault_ids.empty()
                ? options.selected_vault_ids
                : [&]() {
                    std::vector<std::uint32_t> ids;
                    for (const auto& scope : options.vault_scopes) ids.push_back(scope.vault_id);
                    return ids;
                }());

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
    const auto credentials = canManageGatewayCredentials(session)
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
    requireManageCredential(session, *credential);
    return {{"revoked", db::query::s3::Gateway::deleteCredentialByAccessKey(credential->access_key)}};
}

json S3Gateway::credentialsScopeUpdate(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = findCredential(session, payload);
    if (!credential) throw std::runtime_error("S3 gateway credential not found");
    requireManageCredential(session, *credential);
    auto principalId = credential->principal_user_id;
    if (const auto payloadPrincipalId = optionalUInt(payload, "principal_user_id"))
        principalId = *payloadPrincipalId;
    requireCredentialPrincipalMutable(session, principalId);
    const auto scopeMode = normalizeScopeMode(payload.value("scope_mode", credential->scope_mode));
    if (scopeMode == "global")
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials,
            "admin.s3_gateway.manage_credentials is required to retarget global S3 gateway credentials.");
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
        : std::vector<db::query::s3::CredentialVaultAccessShorthand>{};
    const auto payloadDefaultRoleId = defaultVaultRoleIdFromPayload(payload);
    const auto existingDefaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id);
    const auto effectiveDefaultRoleId = payloadDefaultRoleId
        ? payloadDefaultRoleId
        : (existingDefaultRole && existingDefaultRole->enabled
            ? std::make_optional(existingDefaultRole->vault_role_id)
            : std::optional<std::uint32_t>{});
    const auto requestedSelectedVaultIds = (payload.contains("selected_vault_ids") || payload.contains("vault_ids"))
        ? selectedVaultIdsFromPayload(payload)
        : (scopeMode == "vault_allowlist"
            ? enabledSelectedVaultIds(*credential)
            : std::vector<std::uint32_t>{});
    protocols::s3::CredentialManager::validateScopeMutation(
        session->user->id,
        principalId,
        scopeMode,
        requestedScopes,
        requestedSelectedVaultIds,
        effectiveDefaultRoleId);

    db::query::s3::Gateway::updateCredentialScopeMode(
        credential->id,
        scopeMode,
        principalId,
        scopeMode == "global" ? std::make_optional(session->user->id) : credential->created_by,
        description,
        expiresAt,
        enforceLocalBudget);
    if (scopeMode == "user_access")
        db::query::s3::Gateway::replaceCredentialScopeShorthand(credential->id, {});
    else {
        if (payload.contains("vault_scopes"))
            db::query::s3::Gateway::replaceCredentialScopeShorthand(credential->id, requestedScopes);
        else {
            if (payloadDefaultRoleId)
                db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
                    credential->id,
                    *payloadDefaultRoleId,
                    true,
                    session->user->id);
            if (scopeMode == "vault_allowlist" && (payload.contains("selected_vault_ids") || payload.contains("vault_ids")))
                db::query::s3::Gateway::replaceCredentialSelectedVaults(
                    credential->id,
                    requestedSelectedVaultIds,
                    session->user->id);
        }
    }

    auto updated = db::query::s3::Gateway::getCredentialByAccessKey(credential->access_key);
    return {{"credential", updated ? credentialJson(*updated) : json(nullptr)}};
}

json S3Gateway::credentialsDefaultRoleGet(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireViewCredential(session, credential);
    const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    return {
        {"credential", credentialJson(credential)},
        {"default_role", defaultRole ? defaultRoleJson(*defaultRole) : json(nullptr)}
    };
}

json S3Gateway::credentialsDefaultRoleSet(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode == "user_access")
        throw std::runtime_error("user_access S3 gateway credentials do not use gateway vault roles.");
    if (credential.scope_mode == "global")
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials,
            "admin.s3_gateway.manage_credentials is required to set a global S3 gateway credential default role.");
    if (credential.scope_mode == "vault_allowlist")
        requireSelectedVaultGrantPermission(session, credential, enabledSelectedVaultIds(credential));
    const auto role = resolveVaultRole(payload);
    db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
        credential.id,
        role->id,
        payload.value("enabled", true),
        session->user->id);
    const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    return {
        {"credential", credentialJson(credential)},
        {"default_role", defaultRole ? defaultRoleJson(*defaultRole) : json(nullptr)}
    };
}

json S3Gateway::credentialsDefaultRoleClear(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode == "global")
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials,
            "admin.s3_gateway.manage_credentials is required to clear a global S3 gateway credential default role.");
    return {
        {"cleared", db::query::s3::Gateway::deleteCredentialDefaultVaultRole(credential.id)},
        {"credential", credentialJson(credential)}
    };
}

json S3Gateway::credentialsSelectedVaultsList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireViewCredential(session, credential);
    json rows = json::array();
    for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential.id))
        rows.push_back(selectedVaultJson(selectedVault));
    return {{"credential", credentialJson(credential)}, {"selected_vaults", rows}, {"vaults", rows}};
}

json S3Gateway::credentialsSelectedVaultsReplace(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode != "vault_allowlist")
        throw std::runtime_error("selected vaults are only used by vault_allowlist S3 gateway credentials.");
    const auto vaultIds = selectedVaultIdsFromPayload(payload);
    requireSelectedVaultGrantPermission(session, credential, vaultIds);
    db::query::s3::Gateway::replaceCredentialSelectedVaults(credential.id, vaultIds, session->user->id);
    json rows = json::array();
    for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential.id))
        rows.push_back(selectedVaultJson(selectedVault));
    return {{"credential", credentialJson(credential)}, {"selected_vaults", rows}, {"vaults", rows}};
}

json S3Gateway::credentialsSelectedVaultsAdd(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode != "vault_allowlist")
        throw std::runtime_error("selected vaults are only used by vault_allowlist S3 gateway credentials.");
    const auto vault = resolveVault(payload);
    requireSelectedVaultGrantPermission(session, credential, vault->id);
    const auto selectedVault = db::query::s3::Gateway::upsertCredentialSelectedVault(
        credential.id,
        vault->id,
        payload.value("enabled", true),
        session->user->id);
    return {{"credential", credentialJson(credential)}, {"selected_vault", selectedVaultJson(selectedVault)}};
}

json S3Gateway::credentialsSelectedVaultsRemove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode != "vault_allowlist")
        throw std::runtime_error("selected vaults are only used by vault_allowlist S3 gateway credentials.");
    const auto vault = resolveVault(payload);
    requireCredentialVaultRolePermission(
        session,
        credential,
        vault->id,
        vh::rbac::permission::vault::RolePermissions::Revoke,
        "You do not have permission to remove this selected vault from the S3 gateway credential.");
    return {
        {"removed", db::query::s3::Gateway::deleteCredentialSelectedVault(credential.id, vault->id)},
        {"credential", credentialJson(credential)},
        {"vault", vaultJson(vault)}
    };
}

json S3Gateway::credentialsDefaultRoleOverridesList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireViewCredential(session, credential);
    const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    json rows = json::array();
    if (defaultRole) {
        requireDefaultOverridePermission(
            session,
            credential,
            vh::rbac::permission::vault::RolePermissions::ViewOverride,
            "You do not have permission to view default gateway credential role overrides.");
        for (const auto& overrideRule : db::query::s3::Gateway::listCredentialDefaultVaultRoleOverrides(credential.id))
            rows.push_back(defaultOverrideJson(credential, overrideRule));
    }
    return {{"credential", credentialJson(credential)}, {"default_role", defaultRole ? defaultRoleJson(*defaultRole) : json(nullptr)}, {"overrides", rows}};
}

json S3Gateway::credentialsDefaultRoleOverridesAdd(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode == "user_access")
        throw std::runtime_error("user_access S3 gateway credentials do not use gateway vault-role overrides.");
    if (!db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id))
        throw std::runtime_error("A default gateway credential vault role is required before adding default overrides.");
    requireDefaultOverridePermission(
        session,
        credential,
        vh::rbac::permission::vault::RolePermissions::AssignOverride,
        "You do not have permission to add default gateway credential role overrides.");
    auto permission = resolvePermission(payload);
    if (!permission) throw std::runtime_error("permission not found");
    ::vh::rbac::permission::Override overrideRule;
    overrideRule.permission = *permission;
    overrideRule.effect = ::vh::rbac::permission::overrideOptFromString(payload.value("effect", std::string{"allow"}));
    overrideRule.enabled = payload.value("enabled", true);
    overrideRule.pattern = ::vh::rbac::fs::glob::model::Pattern(payload.value("glob_path", payload.value("path", std::string{"**"})));
    const auto id = db::query::s3::Gateway::upsertCredentialDefaultVaultRoleOverride(credential.id, overrideRule);
    for (const auto& saved : db::query::s3::Gateway::listCredentialDefaultVaultRoleOverrides(credential.id)) {
        if (saved.id == id)
            return {{"override", defaultOverrideJson(credential, saved)}};
    }
    throw std::runtime_error("S3 gateway credential default role override was not created.");
}

json S3Gateway::credentialsDefaultRoleOverridesRemove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    requireDefaultOverridePermission(
        session,
        credential,
        vh::rbac::permission::vault::RolePermissions::RevokeOverride,
        "You do not have permission to remove default gateway credential role overrides.");
    auto overrideId = optionalUInt(payload, "override_id");
    if (!overrideId) overrideId = optionalUInt(payload, "id");
    if (!overrideId) throw std::runtime_error("override_id is required");
    return {
        {"removed", db::query::s3::Gateway::deleteCredentialDefaultVaultRoleOverride(credential.id, *overrideId)},
        {"credential", credentialJson(credential)}
    };
}

json S3Gateway::credentialsRolesList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    json rows = json::array();
    for (const auto& assignment : visibleAssignmentsFor(session, credential))
        rows.push_back(assignmentJson(assignment));
    return {{"credential", credentialJson(credential)}, {"roles", rows}, {"assignments", rows}};
}

json S3Gateway::credentialsRolesAssign(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    if (credential.scope_mode == "global")
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials,
            "admin.s3_gateway.manage_credentials is required to manage global S3 gateway credential vault role exceptions.");
    const auto vault = resolveVault(payload);
    const auto role = resolveVaultRole(payload);
    requireSelectedVaultGrantPermission(session, credential, vault->id);

    const auto nextScopeMode = credential.scope_mode == "global"
        ? std::string{"global"}
        : std::string{"vault_allowlist"};
    db::query::s3::Gateway::updateCredentialScopeMode(
        credential.id,
        nextScopeMode,
        credential.principal_user_id,
        credential.created_by,
        credential.description,
        credential.expires_at);
    if (!db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id)) {
        const auto implicitDeny = db::query::rbac::role::Vault::get("implicit_deny");
        db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
            credential.id,
            implicitDeny ? implicitDeny->id : role->id,
            true,
            session->user->id);
    }
    if (nextScopeMode == "vault_allowlist")
        db::query::s3::Gateway::upsertCredentialSelectedVault(
            credential.id,
            vault->id,
            true,
            session->user->id);
    const auto assignmentId = db::query::s3::Gateway::upsertCredentialVaultRoleAssignment({
        .credential_id = credential.id,
        .vault_id = vault->id,
        .vault_role_id = role->id,
        .enabled = payload.value("enabled", true),
        .created_by = session->user->id
    });
    auto assignments = db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    auto it = std::ranges::find_if(assignments, [&](const auto& assignment) {
        return assignment.id == assignmentId;
    });
    if (it == assignments.end()) throw std::runtime_error("S3 gateway credential role assignment was not created.");
    return {{"assignment", assignmentJson(*it)}, {"role", assignmentJson(*it)}};
}

json S3Gateway::credentialsRolesRevoke(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    const auto vault = resolveVault(payload);
    requireCredentialVaultRolePermission(
        session,
        credential,
        vault->id,
        vh::rbac::permission::vault::RolePermissions::Revoke,
        "You do not have permission to revoke gateway credential vault roles for this principal and vault.");
    const auto removed = db::query::s3::Gateway::deleteCredentialVaultRoleAssignment(credential.id, vault->id);
    return {{"revoked", removed}, {"credential", credentialJson(credential)}, {"vault", vaultJson(vault)}};
}

json S3Gateway::credentialsRoleOverridesList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireViewCredential(session, credential);
    const auto vault = resolveVault(payload);
    requireCredentialVaultRolePermission(
        session,
        credential,
        vault->id,
        vh::rbac::permission::vault::RolePermissions::ViewOverride,
        "You do not have permission to view gateway credential vault role overrides for this principal and vault.");
    json rows = json::array();
    if (db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, vault->id)) {
        for (const auto& overrideRule : db::query::s3::Gateway::listCredentialVaultRoleOverrides(credential.id, vault->id))
            rows.push_back(overrideJson(credential, vault, overrideRule));
    }
    return {{"credential", credentialJson(credential)}, {"vault", vaultJson(vault)}, {"overrides", rows}};
}

json S3Gateway::credentialsRoleOverridesAdd(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    const auto vault = resolveVault(payload);
    requireCredentialVaultRolePermission(
        session,
        credential,
        vault->id,
        vh::rbac::permission::vault::RolePermissions::AssignOverride,
        "You do not have permission to add gateway credential vault role overrides for this principal and vault.");
    if (credential.scope_mode == "vault_allowlist") {
        const auto selectedIds = enabledSelectedVaultIds(credential);
        if (!std::ranges::any_of(selectedIds, [&](const auto selectedId) { return selectedId == vault->id; }))
            throw std::runtime_error("Per-vault overrides require the vault to be selected for this credential.");
    }
    if (!db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, vault->id)) {
        const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
        if (!defaultRole || !defaultRole->enabled)
            throw std::runtime_error("A default gateway credential vault role is required before adding per-vault overrides.");
        db::query::s3::Gateway::upsertCredentialVaultRoleAssignment({
            .credential_id = credential.id,
            .vault_id = vault->id,
            .vault_role_id = defaultRole->vault_role_id,
            .enabled = true,
            .created_by = session->user->id
        });
    }
    auto permission = resolvePermission(payload);
    if (!permission) throw std::runtime_error("permission not found");
    ::vh::rbac::permission::Override overrideRule;
    overrideRule.permission = *permission;
    overrideRule.effect = ::vh::rbac::permission::overrideOptFromString(payload.value("effect", std::string{"allow"}));
    overrideRule.enabled = payload.value("enabled", true);
    overrideRule.pattern = ::vh::rbac::fs::glob::model::Pattern(payload.value("glob_path", payload.value("path", std::string{"**"})));
    const auto id = db::query::s3::Gateway::upsertCredentialVaultRoleOverride(credential.id, vault->id, overrideRule);
    for (const auto& saved : db::query::s3::Gateway::listCredentialVaultRoleOverrides(credential.id, vault->id)) {
        if (saved.id == id)
            return {{"override", overrideJson(credential, vault, saved)}};
    }
    throw std::runtime_error("S3 gateway credential vault role override was not created.");
}

json S3Gateway::credentialsRoleOverridesRemove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto credential = requireCredentialFromPayload(session, payload);
    requireManageCredential(session, credential);
    const auto vault = resolveVault(payload);
    requireCredentialVaultRolePermission(
        session,
        credential,
        vault->id,
        vh::rbac::permission::vault::RolePermissions::RevokeOverride,
        "You do not have permission to remove gateway credential vault role overrides for this principal and vault.");
    auto overrideId = optionalUInt(payload, "override_id");
    if (!overrideId) overrideId = optionalUInt(payload, "id");
    if (!overrideId) throw std::runtime_error("override_id is required");
    if (!db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, vault->id))
        return {
            {"removed", false},
            {"credential", credentialJson(credential)},
            {"vault", vaultJson(vault)}
        };
    return {
        {"removed", db::query::s3::Gateway::deleteCredentialVaultRoleOverride(credential.id, vault->id, *overrideId)},
        {"credential", credentialJson(credential)},
        {"vault", vaultJson(vault)}
    };
}

json S3Gateway::bucketsList(const json&, const std::shared_ptr<Session>& session) {
    json rows = json::array();
    for (const auto& bucket : protocols::s3::ObjectStore{}.listBuckets(session->user))
        rows.push_back(bucketJson(bucket));
    return {{"buckets", rows}};
}

json S3Gateway::bucketsBind(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vault = resolveVault(payload);
    const auto bucketName = optionalString(payload, "bucket_name").value_or(vault->slug);
    requireGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
        "admin.s3_gateway.manage_buckets is required to bind S3 gateway buckets.");
    requireVaultEdit(session, vault->id);
    db::query::s3::Gateway::bindBucket({
        .vault_id = vault->id,
        .bucket_name = bucketName,
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
    requireGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
        "admin.s3_gateway.manage_buckets is required to unbind S3 gateway buckets.");
    requireVaultEdit(session, binding->vault_id);
    return {{"unbound", db::query::s3::Gateway::unbindBucket(bucketName)}};
}

json S3Gateway::bucketsCreateLocal(const json& payload, const std::shared_ptr<Session>& session) {
    const auto ownerId = payload.value("owner_id", session->user->id);
    if (ownerId != session->user->id)
        requireGatewayPermission(
            session,
            vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
            "admin.s3_gateway.manage_buckets is required to create local gateway buckets for another owner.");
    requireVaultCreateForOwner(session, ownerId);
    auto owner = db::query::identities::User::getUserById(ownerId);
    if (!owner) throw std::runtime_error("Owner user not found.");

    if (const auto bucketName = optionalString(payload, "bucket_name")) {
        const auto bucket = protocols::s3::ObjectStore{}.createBucket(
            *bucketName,
            session->user,
            owner->id,
            "local",
            payload.value("quota_bytes", static_cast<uintmax_t>(0)));
        return {{"bucket", bucketJson(*db::query::s3::Gateway::resolveBucket(bucket.bucket_name))}};
    }

    const auto displayName = optionalString(payload, "name")
        .value_or("S3 gateway local bucket for " + owner->name);
    auto vault = std::make_shared<::vh::vault::model::Vault>();
    vault->name = displayName;
    vault->description = payload.value("description", "S3 gateway bucket " + displayName);
    vault->owner_id = owner->id;
    vault->type = ::vh::vault::model::VaultType::Local;
    vault->quota = payload.value("quota_bytes", static_cast<uintmax_t>(0));
    vault->is_active = true;

    auto sync = std::make_shared<sync::model::LocalPolicy>();
    sync->conflict_policy = sync::model::LocalPolicy::ConflictPolicy::KeepBoth;

    vault = runtime::Deps::get().storageManager->addVault(vault, sync);
    db::query::s3::Gateway::bindBucket({
        .vault_id = vault->id,
        .bucket_name = vault->slug,
        .api_exclusive = config::Registry::get().s3_gateway.default_api_exclusive,
        .mode = "local",
        .created_by = session->user->id
    });
    return {{"bucket", bucketJson(*db::query::s3::Gateway::resolveBucket(vault->slug))}};
}

json S3Gateway::bucketsCreateRemoteCache(const json& payload, const std::shared_ptr<Session>& session) {
    requireGatewayPermission(
        session,
        vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
        "admin.s3_gateway.manage_buckets is required to create remote-cache S3 gateway buckets.");
    const auto apiKey = resolveApiKey(payload);
    if (!apiKey) throw std::runtime_error("Upstream API key not found.");
    const auto ownerId = payload.value("owner_id", session->user->id);
    requireVaultCreateForOwner(session, ownerId);

    const auto bucketName = optionalString(payload, "bucket_name");
    if (bucketName) ::vh::vault::model::requireValidS3Name(*bucketName);

    auto vault = std::make_shared<::vh::vault::model::S3Vault>();
    vault->name = optionalString(payload, "name").value_or(bucketName.value_or("S3 gateway remote-cache bucket"));
    vault->description = payload.value("description", "S3 gateway remote-cache bucket " + vault->name);
    vault->owner_id = ownerId;
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
    const auto effectiveBucketName = bucketName.value_or(created->slug);
    db::query::s3::Gateway::bindBucket({
        .vault_id = created->id,
        .bucket_name = effectiveBucketName,
        .api_exclusive = true,
        .mode = "remote_cache",
        .created_by = session->user->id
    });
    return {{"bucket", bucketJson(*db::query::s3::Gateway::resolveBucket(effectiveBucketName))}};
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
