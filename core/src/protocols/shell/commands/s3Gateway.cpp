#include "protocols/shell/commands/all.hpp"

#include "config/Registry.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/rbac/Permission.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "protocols/s3/CredentialManager.hpp"
#include "protocols/s3/GatewayService.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/permission/admin/S3Gateway.hpp"
#include "rbac/permission/vault/Roles.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "runtime/Deps.hpp"
#include "runtime/Manager.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "usage/include/UsageManager.hpp"
#include "fs/model/File.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <openssl/md5.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace vh::protocols::shell::commands {

namespace {

std::string yesNo(const bool value) {
    return value ? "yes" : "no";
}

std::vector<std::string> optVals(const CommandCall& call, const std::string& key) {
    std::vector<std::string> values;
    for (const auto& [k, v] : call.options)
        if (k == key && v) values.push_back(*v);
    return values;
}

std::string normalizeScopeMode(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return c == '-' ? '_' : static_cast<char>(std::tolower(c));
    });
    if (value == "user_access" || value == "global" || value == "vault_allowlist") return value;
    throw std::runtime_error("s3-gateway: scope must be user-access, global, or vault-allowlist");
}

std::optional<std::time_t> parseExpiresAt(const CommandCall& call) {
    const auto value = optVal(call, "expires");
    if (!value || value->empty()) return std::nullopt;
    const auto suffix = value->back();
    const auto number = (suffix >= '0' && suffix <= '9') ? *value : value->substr(0, value->size() - 1);
    const auto parsed = parseUInt(number);
    if (!parsed || *parsed == 0) throw std::runtime_error("s3-gateway: --expires must be a positive duration such as 30d or 12h");
    std::time_t seconds = *parsed;
    switch (std::tolower(static_cast<unsigned char>(suffix))) {
    case 'd': seconds *= 24 * 60 * 60; break;
    case 'h': seconds *= 60 * 60; break;
    case 'm': seconds *= 60; break;
    case 's': break;
    default:
        if (suffix < '0' || suffix > '9') throw std::runtime_error("s3-gateway: unsupported --expires suffix");
    }
    return std::time(nullptr) + seconds;
}

std::optional<db::query::s3::GatewayCredential> resolveCredentialForCaller(
    const CommandCall& call,
    const std::string& value) {
    const auto credentials = ::vh::rbac::resolver::Admin::has<::vh::rbac::permission::admin::S3GatewayPermissions>({
            .user = call.user,
            .permission = ::vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials
        })
        ? db::query::s3::Gateway::listCredentialsAdmin(true)
        : db::query::s3::Gateway::listCredentialsForPrincipal(call.user->id);
    for (const auto& credential : credentials) {
        if (credential.access_key == value || credential.name == value || std::to_string(credential.id) == value)
            return credential;
    }
    return std::nullopt;
}

std::optional<db::query::s3::GatewayCredential> resolveCredentialForGatewayBudget(
    const CommandCall& call,
    const std::string& value,
    const bool allowAnyCredential) {
    if (!allowAnyCredential) return resolveCredentialForCaller(call, value);
    const auto credentials = db::query::s3::Gateway::listCredentialsAdmin(true);
    for (const auto& credential : credentials) {
        if (credential.access_key == value || credential.name == value || std::to_string(credential.id) == value)
            return credential;
    }
    return std::nullopt;
}

bool callerOwnsCredential(const CommandCall& call, const std::uint32_t credentialId) {
    const auto owned = db::query::s3::Gateway::listCredentialsForPrincipal(call.user->id);
    return std::ranges::any_of(owned, [&](const auto& credential) {
        return credential.id == credentialId;
    });
}

bool hasGatewayPermission(
    const CommandCall& call,
    const ::vh::rbac::permission::admin::S3GatewayPermissions permission) {
    if (!call.user) return false;
    if (call.user->isSuperAdmin()) return true;
    using Perm = ::vh::rbac::permission::admin::S3GatewayPermissions;
    return ::vh::rbac::resolver::Admin::has<Perm>({
        .user = call.user,
        .permission = permission
    });
}

std::optional<CommandResult> requireGatewayPermissionCommand(
    const CommandCall& call,
    const ::vh::rbac::permission::admin::S3GatewayPermissions permission,
    const std::string& action) {
    if (!hasGatewayPermission(call, permission))
        return invalid("s3-gateway " + action + ": admin.s3_gateway permission is required");
    return std::nullopt;
}

bool canManageGatewayCredentials(const CommandCall& call) {
    return hasGatewayPermission(call, ::vh::rbac::permission::admin::S3GatewayPermissions::ManageCredentials);
}

bool canAssignGatewayPrincipal(const CommandCall& call) {
    return hasGatewayPermission(call, ::vh::rbac::permission::admin::S3GatewayPermissions::AssignPrincipal);
}

bool canManageGatewayBuckets(const CommandCall& call) {
    return hasGatewayPermission(call, ::vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets);
}

bool canManageGatewayBudgets(const CommandCall& call) {
    return hasGatewayPermission(call, ::vh::rbac::permission::admin::S3GatewayPermissions::ManageBudgets);
}

bool canViewGatewayBudgets(const CommandCall& call) {
    return canManageGatewayBudgets(call) ||
        hasGatewayPermission(call, ::vh::rbac::permission::admin::S3GatewayPermissions::View);
}

std::shared_ptr<::vh::vault::model::Vault> resolveVaultArg(const CommandCall& call, const std::string& value);

std::optional<uint32_t> defaultRoleIdFromOptions(const CommandCall& call, const std::string& errPrefix) {
    auto roleValue = optVal(call, "default-role");
    if (!roleValue || roleValue->empty()) roleValue = optVal(call, "default-vault-role");
    if (!roleValue || roleValue->empty()) roleValue = optVal(call, "role");
    if (!roleValue || roleValue->empty()) return std::nullopt;
    const auto role = resolveVaultRole(*roleValue, errPrefix);
    if (!role) throw std::runtime_error(role.error);
    return role.ptr->id;
}

std::vector<uint32_t> selectedVaultIdsFromOptions(const CommandCall& call) {
    std::vector<uint32_t> vaultIds;
    for (const auto& vaultValue : optVals(call, "selected-vault")) {
        const auto vault = resolveVaultArg(call, vaultValue);
        vaultIds.push_back(vault->id);
    }
    return vaultIds;
}

std::vector<db::query::s3::CredentialVaultAccessShorthand> scopesFromVaultOptions(const CommandCall& call, const uint32_t credentialId = 0) {
    std::vector<db::query::s3::CredentialVaultAccessShorthand> scopes;
    for (const auto& vaultValue : optVals(call, "vault")) {
        const auto vault = resolveVaultArg(call, vaultValue);
        scopes.push_back({
            .credential_id = credentialId,
            .vault_id = vault->id,
            .can_list = hasFlag(call, "list") || (!hasFlag(call, "read") && !hasFlag(call, "write") && !hasFlag(call, "delete") && !hasFlag(call, "admin")),
            .can_read = hasFlag(call, "read") || (!hasFlag(call, "write") && !hasFlag(call, "delete") && !hasFlag(call, "admin")),
            .can_write = hasFlag(call, "write"),
            .can_delete = hasFlag(call, "delete"),
            .can_admin = hasFlag(call, "admin")
        });
    }
    return scopes;
}

db::query::s3::CredentialVaultAccessShorthand scopeFromVaultRole(
    const uint32_t credentialId,
    const uint32_t vaultId,
    const std::shared_ptr<::vh::rbac::role::Vault>& role) {
    if (!role) throw std::runtime_error("s3-gateway creds role: role not found");
    return {
        .credential_id = credentialId,
        .vault_id = vaultId,
        .can_list = role->directories().canList(),
        .can_read = role->files().canDownload() || role->directories().canDownload(),
        .can_write = role->files().canUpload() || role->files().canOverwrite() ||
                     role->directories().canUpload() || role->directories().canTouch(),
        .can_delete = role->files().canDelete() || role->directories().canDelete(),
        .can_admin = role->rolesPerms().canAssign() || role->rolesPerms().canModify() ||
                     role->rolesPerms().canRevoke()
    };
}

std::optional<CommandResult> validateGatewayCredentialRoleMutation(
    const CommandCall& call,
    const db::query::s3::GatewayCredential& credential,
    const db::query::s3::CredentialVaultAccessShorthand& scope) {
    try {
        protocols::s3::CredentialManager::validateScopeMutation(
            call.user->id,
            credential.principal_user_id,
            "vault_allowlist",
            {scope});
    } catch (const std::exception& e) {
        return invalid(e.what());
    }
    return std::nullopt;
}

std::string roleNameFromScopeShorthand(const db::query::s3::CredentialVaultAccessShorthand& scope) {
    if (scope.can_admin) return "manager";
    if (scope.can_delete) return "manager";
    if (scope.can_write) return "contributor";
    if (scope.can_read) return "reader";
    if (scope.can_list) return "guest";
    return "implicit_deny";
}

std::shared_ptr<::vh::rbac::role::Vault> roleForScopeShorthand(
    const db::query::s3::CredentialVaultAccessShorthand& scope) {
    auto role = db::query::rbac::role::Vault::get(roleNameFromScopeShorthand(scope));
    if (!role) throw std::runtime_error("s3-gateway creds scope: inferred vault role is not seeded");
    return role;
}

std::optional<CommandResult> requireGatewayCredentialVaultRolePermission(
    const CommandCall& call,
    const db::query::s3::GatewayCredential& credential,
    const uint32_t vaultId,
    const ::vh::rbac::permission::vault::RolePermissions permission,
    const std::string& action) {
    if (credential.principal_user_id != call.user->id && !canAssignGatewayPrincipal(call))
        return invalid("s3-gateway creds role " + action + ": admin.s3_gateway.assign_principal is required for another principal");
    if (!::vh::rbac::resolver::Vault::has<::vh::rbac::permission::vault::RolePermissions>({
        .user = call.user,
        .permission = permission,
        .target_subject_type = std::string("user"),
        .target_subject_id = credential.principal_user_id,
        .vault_id = vaultId
    }))
        return invalid("s3-gateway creds role " + action + ": you do not have vault role permission for this principal and vault");
    return std::nullopt;
}

std::shared_ptr<::vh::rbac::permission::Permission> resolveGatewayOverridePermission(
    const CommandCall& call,
    const std::string& errPrefix) {
    auto value = optVal(call, "permission");
    if (!value || value->empty()) value = optVal(call, "perm");
    if (!value || value->empty()) throw std::runtime_error(errPrefix + ": --permission is required");

    if (const auto id = parseUInt(*value))
        return db::query::rbac::Permission::getPermission(*id);

    const auto direct = *value;
    std::vector<std::string> candidates{direct};
    if (direct.find('.') == std::string::npos) {
        candidates.push_back("vault.fs.files." + direct);
        candidates.push_back("vault.fs.directories." + direct);
    }
    for (const auto& candidate : candidates) {
        try {
            return db::query::rbac::Permission::getPermissionByName(candidate);
        } catch (const std::exception&) {
        }
    }
    throw std::runtime_error(errPrefix + ": permission not found '" + direct + "'");
}

::vh::rbac::permission::OverrideOpt resolveGatewayOverrideEffect(const CommandCall& call, const std::string& errPrefix) {
    const auto parsed = ::vh::protocols::shell::commands::vault::parseEffectChangeOpt(call, errPrefix);
    if (!parsed.ok) throw std::runtime_error(parsed.error);
    if (parsed.value) return *parsed.value;

    if (const auto value = optVal(call, "effect"); value && !value->empty())
        return ::vh::rbac::permission::overrideOptFromString(*value);

    throw std::runtime_error(errPrefix + ": --effect allow|deny is required");
}

std::string gwValueOrDash(const std::optional<std::string>& value) {
    return value && !value->empty() ? *value : "-";
}

std::string gwValueOrDash(const std::optional<std::uint32_t>& value) {
    return value ? std::to_string(*value) : "-";
}

std::string renderGatewayBudgetPolicies(const std::vector<storage::s3::pricing::PriceBudgetPolicy>& policies) {
    if (policies.empty()) return "No S3 gateway budget policies configured.\n";
    Table table({
        {"ID", Align::Right, 2, 6, false, false},
        {"Scope", Align::Left, 8, 26, false, false},
        {"Key", Align::Right, 1, 8, false, false},
        {"Vault", Align::Right, 1, 8, false, false},
        {"Mode", Align::Left, 3, 8, false, false},
        {"Monthly", Align::Right, 1, 14, false, false},
        {"Currency", Align::Left, 3, 8, false, false},
        {"Active", Align::Left, 3, 6, false, false}
    });
    for (const auto& policy : policies) {
        table.add_row({
            std::to_string(policy.id),
            storage::s3::pricing::toString(policy.scope),
            gwValueOrDash(policy.gateway_credential_id),
            gwValueOrDash(policy.vault_id),
            storage::s3::pricing::toString(policy.mode),
            gwValueOrDash(policy.max_monthly_cost),
            policy.currency,
            yesNo(policy.is_active)
        });
    }
    return table.render();
}

std::string renderGatewayBudgetTrends(const std::vector<storage::s3::pricing::PriceBudgetTrendStats>& trends) {
    if (trends.empty()) return "No S3 gateway budget usage yet.\n";
    Table table({
        {"Policy", Align::Right, 2, 6, false, false},
        {"Scope", Align::Left, 8, 26, false, false},
        {"Key", Align::Right, 1, 8, false, false},
        {"Vault", Align::Right, 1, 8, false, false},
        {"Window", Align::Left, 5, 8, false, false},
        {"Used", Align::Right, 1, 14, false, false},
        {"Remaining", Align::Right, 1, 14, false, false},
        {"Limit", Align::Right, 1, 14, false, false},
        {"Currency", Align::Left, 3, 8, false, false}
    });
    for (const auto& trend : trends) {
        table.add_row({
            std::to_string(trend.policy_id),
            trend.scope,
            gwValueOrDash(trend.gateway_credential_id),
            gwValueOrDash(trend.vault_id),
            trend.window_type,
            trend.total_cost,
            gwValueOrDash(trend.remaining),
            gwValueOrDash(trend.limit),
            trend.currency
        });
    }
    return table.render();
}

bool isGatewayMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"s3-gateway", cmd}, input);
}

std::shared_ptr<identities::User> resolveTargetUser(const CommandCall& call) {
    const auto userOpt = optVal(call, "user");
    if (!userOpt || userOpt->empty()) return call.user;
    auto target = resolveUser(*userOpt, "s3-gateway");
    if (!target) throw std::runtime_error(target.error);
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
    if (vault->owner_id == call.user->id) return;
    using Perm = vh::rbac::permission::admin::VaultPermissions;
    if (!vh::rbac::resolver::Admin::has<Perm>({
            .user = call.user,
            .permission = Perm::Edit,
            .vault_id = vault->id
        }))
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

void validateBucketModeForVault(
    const std::shared_ptr<::vh::vault::model::Vault>& vault,
    const std::string& mode,
    const std::string& action) {
    if (!vault) throw std::runtime_error("s3-gateway " + action + ": vault not found");
    const bool s3Backed = vault->type == ::vh::vault::model::VaultType::S3;
    if (s3Backed && mode == "local")
        throw std::runtime_error("s3-gateway " + action + ": S3/R2 vaults must use remote_cache or remote_proxy mode");
    if (!s3Backed && mode != "local")
        throw std::runtime_error("s3-gateway " + action + ": local vaults can only use local mode");
}

std::string gatewayObjectKeyFromPath(const std::filesystem::path& path) {
    auto key = path.lexically_normal().generic_string();
    while (!key.empty() && key.front() == '/') key.erase(key.begin());
    return key;
}

std::string hexDigest(const unsigned char* digest, const std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i)
        out << std::setw(2) << static_cast<unsigned int>(digest[i]);
    return out.str();
}

std::vector<uint8_t> readFileBytesForGatewayBackfill(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("unable to read backing file for ETag backfill: " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string md5EtagForGatewayBackfill(const std::vector<uint8_t>& bytes) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(bytes.data(), bytes.size(), digest);
    return "\"" + hexDigest(digest, MD5_DIGEST_LENGTH) + "\"";
}

std::vector<uint8_t> plaintextForGatewayBackfill(
    const std::shared_ptr<storage::Engine>& engine,
    const std::shared_ptr<::vh::fs::model::File>& file) {
    if (!file || file->size_bytes == 0) return {};
    if (file->encryption_iv.empty() || file->encrypted_with_key_version == 0)
        return readFileBytesForGatewayBackfill(file->backing_path);
    return engine->decrypt(file);
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

std::optional<CommandResult> requireAdminCommand(const CommandCall& call, const std::string& action) {
    return requireGatewayPermissionCommand(
        call,
        ::vh::rbac::permission::admin::S3GatewayPermissions::View,
        action);
}

bool canCreateVaultForOwner(const CommandCall& call, const std::uint32_t ownerId) {
    if (!call.user) return false;
    using Perm = vh::rbac::permission::admin::VaultPermissions;
    return vh::rbac::resolver::Admin::has<Perm>({
        .user = call.user,
        .permission = Perm::Create,
        .target_user_id = ownerId
    });
}

CommandResult handleS3GatewayStatus(const CommandCall& call) {
    if (auto adminError = requireAdminCommand(call, "status")) return *adminError;
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

CommandResult handleEnable(const CommandCall& call) {
    if (auto adminError = requireGatewayPermissionCommand(
            call,
            ::vh::rbac::permission::admin::S3GatewayPermissions::ManageService,
            "enable")) return *adminError;
    auto cfg = config::Registry::get();
    cfg.s3_gateway.enabled = true;
    return saveConfigAndRestart(cfg, "S3 gateway enabled.\n");
}

CommandResult handleDisable(const CommandCall& call) {
    if (auto adminError = requireGatewayPermissionCommand(
            call,
            ::vh::rbac::permission::admin::S3GatewayPermissions::ManageService,
            "disable")) return *adminError;
    auto cfg = config::Registry::get();
    cfg.s3_gateway.enabled = false;
    return saveConfigAndRestart(cfg, "S3 gateway disabled.\n");
}

CommandResult handleCredsCreate(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto user = resolveTargetUser(call);
    if (user->id != call.user->id && !canAssignGatewayPrincipal(call))
        return invalid("s3-gateway creds create: admin.s3_gateway.assign_principal is required");
    const protocols::s3::CredentialManager manager;
    protocols::s3::CredentialCreateOptions options;
    options.created_by = call.user->id;
    options.principal_user_id = user->id;
    options.name = call.positionals[0];
    options.scope_mode = normalizeScopeMode(optVal(call, "scope").value_or("user_access"));
    options.description = optVal(call, "description");
    options.expires_at = parseExpiresAt(call);
    try {
        options.default_vault_role_id = defaultRoleIdFromOptions(call, "s3-gateway creds create");
        options.selected_vault_ids = selectedVaultIdsFromOptions(call);
    } catch (const std::exception& e) {
        return invalid(e.what());
    }
    options.vault_scopes = scopesFromVaultOptions(call);
    options.enforce_budget_for_local_requests = hasFlag(call, "enforce-budget-for-local-requests");
    if ((!options.vault_scopes.empty() || !options.selected_vault_ids.empty()) && options.scope_mode == "user_access")
        options.scope_mode = "vault_allowlist";
    if (options.scope_mode == "global" && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds create: admin.s3_gateway.manage_credentials is required for global credentials");
    const auto secret = manager.createCredential(options);

    if (hasFlag(call, "json")) {
        nlohmann::json j = {
            {"user_id", user->id},
            {"principal_user_id", secret.credential.principal_user_id},
            {"created_by", secret.credential.created_by ? nlohmann::json(*secret.credential.created_by) : nlohmann::json(nullptr)},
            {"name", call.positionals[0]},
            {"access_key", secret.credential.access_key},
            {"secret_access_key", secret.secret_access_key},
            {"scope_mode", secret.credential.scope_mode},
            {"enforce_budget_for_local_requests", secret.credential.enforce_budget_for_local_requests},
            {"expires_at", secret.credential.expires_at ? nlohmann::json(*secret.credential.expires_at) : nlohmann::json(nullptr)}
        };
        return ok(j.dump(4) + "\n");
    }

    std::ostringstream out;
    out << "created S3 gateway credential\n";
    out << "  user: " << user->name << " (" << user->id << ")\n";
    out << "  name: " << call.positionals[0] << "\n";
    out << "  scope: " << secret.credential.scope_mode << "\n";
    out << "  count local/cache budget usage: " << yesNo(secret.credential.enforce_budget_for_local_requests) << "\n";
    out << "  access key: " << secret.credential.access_key << "\n";
    out << "  secret access key: " << secret.secret_access_key << "\n";
    if (!options.vault_scopes.empty())
        out << "  warning: Boolean scope flags are compatibility shorthand. Gateway authorization uses vault roles.\n";
    out << "store the secret now; it cannot be listed again.\n";
    return ok(out.str());
}

CommandResult handleCredsList(const CommandCall& call) {
    const auto user = resolveTargetUser(call);
    if (user->id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds list: admin.s3_gateway.manage_credentials is required to list another principal's credentials");
    const auto creds = db::query::s3::Gateway::listCredentials(user->id);

    if (hasFlag(call, "json")) {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& credential : creds) {
            rows.push_back({
                {"id", credential.id},
                {"user_id", credential.user_id},
                {"principal_user_id", credential.principal_user_id},
                {"created_by", credential.created_by ? nlohmann::json(*credential.created_by) : nlohmann::json(nullptr)},
                {"name", credential.name},
                {"access_key", credential.access_key},
                {"scope_mode", credential.scope_mode},
                {"enforce_budget_for_local_requests", credential.enforce_budget_for_local_requests},
                {"description", credential.description ? nlohmann::json(*credential.description) : nlohmann::json(nullptr)},
                {"enabled", credential.enabled},
                {"created_at", credential.created_at},
                {"last_used_at", credential.last_used_at ? nlohmann::json(*credential.last_used_at) : nlohmann::json(nullptr)},
                {"expires_at", credential.expires_at ? nlohmann::json(*credential.expires_at) : nlohmann::json(nullptr)}
            });
        }
        return ok(rows.dump(4) + "\n");
    }

    std::ostringstream out;
    out << "s3 gateway credentials for " << user->name << " (" << user->id << ")\n";
    if (creds.empty()) out << "none\n";
    for (const auto& credential : creds) {
        out << "- " << credential.name << " " << credential.access_key
            << " scope=" << credential.scope_mode
            << " local_budget=" << yesNo(credential.enforce_budget_for_local_requests)
            << " enabled=" << yesNo(credential.enabled);
        if (credential.last_used_at) out << " last_used=" << *credential.last_used_at;
        if (credential.expires_at) out << " expires=" << *credential.expires_at;
        out << "\n";
    }
    return ok(out.str());
}

CommandResult handleCredsRevoke(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto value = call.positionals[0];
    const auto credential = resolveCredentialForCaller(call, value);
    if (!credential) return invalid("s3-gateway creds revoke: credential not found");
    const auto removed = db::query::s3::Gateway::deleteCredentialByAccessKey(credential->access_key);

    if (!removed) return invalid("s3-gateway creds revoke: credential not found");
    return ok("S3 gateway credential revoked.\n");
}

std::string renderCredentialScopes(const db::query::s3::GatewayCredential& credential) {
    std::ostringstream out;
    out << "S3 gateway credential policy\n";
    out << "  name: " << credential.name << "\n";
    out << "  access key: " << credential.access_key << "\n";
    out << "  principal: " << credential.principal_user_id << "\n";
    out << "  scope: " << credential.scope_mode << "\n";
    out << "  count local/cache budget usage: " << yesNo(credential.enforce_budget_for_local_requests) << "\n";
    if (credential.scope_mode == "user_access") {
        out << "  gateway vault policy: none; uses principal RBAC\n";
        return out.str();
    }

    const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    if (defaultRole) {
        const auto role = db::query::rbac::role::Vault::get(defaultRole->vault_role_id);
        out << "  default role: " << (role ? role->name : std::to_string(defaultRole->vault_role_id))
            << " enabled=" << yesNo(defaultRole->enabled) << "\n";
    } else {
        out << "  default role: none\n";
    }

    if (credential.scope_mode == "vault_allowlist") {
        const auto selectedVaults = db::query::s3::Gateway::listCredentialSelectedVaults(credential.id);
        out << "  selected vaults:";
        if (selectedVaults.empty()) {
            out << " none\n";
        } else {
            out << "\n";
            for (const auto& selectedVault : selectedVaults)
                out << "  - vault=" << selectedVault.vault_id
                    << " enabled=" << yesNo(selectedVault.enabled) << "\n";
        }
    } else {
        out << "  selected vaults: gateway bucket bindings\n";
    }

    const auto assignments = db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    out << "  per-vault exceptions:";
    if (assignments.empty()) {
        out << " none\n";
    } else {
        out << "\n";
        for (const auto& assignment : assignments) {
            const auto role = db::query::rbac::role::Vault::get(assignment.vault_role_id);
            out << "  - vault=" << assignment.vault_id
                << " role=" << (role ? role->name : std::to_string(assignment.vault_role_id))
                << " enabled=" << yesNo(assignment.enabled) << "\n";
        }
    }
    return out.str();
}

CommandResult handleCredsScope(const CommandCall& call) {
    if (call.positionals.size() < 2) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds scope: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds scope: permission denied");

    const auto action = call.positionals[1];
    if (action == "show" || action == "list") {
        if (hasFlag(call, "json")) {
            const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id);
            nlohmann::json selectedVaults = nlohmann::json::array();
            for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential->id))
                selectedVaults.push_back({
                    {"credential_id", selectedVault.credential_id},
                    {"vault_id", selectedVault.vault_id},
                    {"enabled", selectedVault.enabled}
                });
            nlohmann::json roleAssignments = nlohmann::json::array();
            for (const auto& assignment : db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential->id))
                roleAssignments.push_back({
                    {"id", assignment.id},
                    {"credential_id", assignment.credential_id},
                    {"vault_id", assignment.vault_id},
                    {"vault_role_id", assignment.vault_role_id},
                    {"enabled", assignment.enabled}
                });
            return ok(nlohmann::json{
                {"credential_id", credential->id},
                {"scope_mode", credential->scope_mode},
                {"enforce_budget_for_local_requests", credential->enforce_budget_for_local_requests},
                {"default_role", defaultRole ? nlohmann::json{
                    {"id", defaultRole->id},
                    {"credential_id", defaultRole->credential_id},
                    {"vault_role_id", defaultRole->vault_role_id},
                    {"enabled", defaultRole->enabled}
                } : nlohmann::json(nullptr)},
                {"selected_vaults", selectedVaults},
                {"role_assignments", roleAssignments}
            }.dump(4) + "\n");
        }
        return ok(renderCredentialScopes(*credential));
    }

    if (action == "set") {
        const auto scopeOpt = optVal(call, "scope");
        if (hasFlag(call, "enforce-budget-for-local-requests") && hasFlag(call, "no-enforce-budget-for-local-requests"))
            return invalid("s3-gateway creds scope set: local budget enforcement flags are mutually exclusive");
        std::optional<bool> enforceLocalBudget;
        if (hasFlag(call, "enforce-budget-for-local-requests")) enforceLocalBudget = true;
        if (hasFlag(call, "no-enforce-budget-for-local-requests")) enforceLocalBudget = false;
        if ((!scopeOpt || scopeOpt->empty()) &&
            !enforceLocalBudget &&
            !hasKey(call, "description") &&
            !hasKey(call, "expires") &&
            !hasKey(call, "user") &&
            !hasKey(call, "default-role") &&
            !hasKey(call, "default-vault-role") &&
            !hasKey(call, "role") &&
            optVals(call, "selected-vault").empty())
            return invalid("s3-gateway creds scope set: --scope or a setting flag is required");
        const auto scopeMode = scopeOpt && !scopeOpt->empty()
            ? normalizeScopeMode(*scopeOpt)
            : credential->scope_mode;
        if (scopeMode == "global" && !canManageGatewayCredentials(call))
            return invalid("s3-gateway creds scope set: admin.s3_gateway.manage_credentials is required for global scope");
        auto principalUserId = credential->principal_user_id;
        if (const auto userOpt = optVal(call, "user"); userOpt && !userOpt->empty()) {
            auto target = resolveUser(*userOpt, "s3-gateway creds scope set");
            if (!target) return invalid(target.error);
            if (target.ptr->id != call.user->id && !canAssignGatewayPrincipal(call))
                return invalid("s3-gateway creds scope set: admin.s3_gateway.assign_principal is required");
            principalUserId = target.ptr->id;
        }
        const auto description = hasKey(call, "description")
            ? optVal(call, "description")
            : credential->description;
        const auto expiresAt = hasKey(call, "expires")
            ? parseExpiresAt(call)
            : credential->expires_at;
        std::optional<uint32_t> payloadDefaultRoleId;
        std::vector<uint32_t> selectedVaultIds;
        try {
            payloadDefaultRoleId = defaultRoleIdFromOptions(call, "s3-gateway creds scope set");
            selectedVaultIds = !optVals(call, "selected-vault").empty()
                ? selectedVaultIdsFromOptions(call)
                : [&]() {
                    std::vector<uint32_t> ids;
                    for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential->id))
                        if (selectedVault.enabled) ids.push_back(selectedVault.vault_id);
                    return ids;
                }();
        } catch (const std::exception& e) {
            return invalid(e.what());
        }
        const auto existingDefaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id);
        const auto effectiveDefaultRoleId = payloadDefaultRoleId
            ? payloadDefaultRoleId
            : (existingDefaultRole && existingDefaultRole->enabled
                ? std::make_optional(existingDefaultRole->vault_role_id)
                : std::optional<uint32_t>{});
        try {
            protocols::s3::CredentialManager::validateScopeMutation(
                call.user->id,
                principalUserId,
                scopeMode,
                {},
                selectedVaultIds,
                effectiveDefaultRoleId);
        } catch (const std::exception& e) {
            return invalid(e.what());
        }
        const auto createdBy = scopeMode == "global"
            ? std::make_optional(call.user->id)
            : credential->created_by;
        db::query::s3::Gateway::updateCredentialScopeMode(
            credential->id,
            scopeMode,
            principalUserId,
            createdBy,
            description,
            expiresAt,
            enforceLocalBudget);
        if (scopeMode == "user_access")
            db::query::s3::Gateway::replaceCredentialScopeShorthand(credential->id, {});
        else {
            if (payloadDefaultRoleId)
                db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
                    credential->id,
                    *payloadDefaultRoleId,
                    true,
                    call.user->id);
            if (!optVals(call, "selected-vault").empty())
                db::query::s3::Gateway::replaceCredentialSelectedVaults(
                    credential->id,
                    selectedVaultIds,
                    call.user->id);
        }
        return ok("S3 gateway credential scope updated.\n");
    }

    if (action == "allow-vault") {
        const auto vaultValue = call.positionals.size() >= 3
            ? call.positionals[2]
            : optVal(call, "vault").value_or("");
        if (vaultValue.empty()) return invalid("s3-gateway creds scope allow-vault: vault is required");
        const auto vault = resolveVaultArg(call, vaultValue);
        db::query::s3::CredentialVaultAccessShorthand scope{
            .credential_id = credential->id,
            .vault_id = vault->id,
            .can_list = hasFlag(call, "list") || (!hasFlag(call, "read") && !hasFlag(call, "write") && !hasFlag(call, "delete") && !hasFlag(call, "admin")),
            .can_read = hasFlag(call, "read") || (!hasFlag(call, "write") && !hasFlag(call, "delete") && !hasFlag(call, "admin")),
            .can_write = hasFlag(call, "write"),
            .can_delete = hasFlag(call, "delete"),
            .can_admin = canManageGatewayCredentials(call) && hasFlag(call, "admin")
        };
        auto selectedVaultIds = [&]() {
            std::vector<uint32_t> ids;
            for (const auto& selectedVault : db::query::s3::Gateway::listCredentialSelectedVaults(credential->id))
                if (selectedVault.enabled && selectedVault.vault_id != vault->id) ids.push_back(selectedVault.vault_id);
            ids.push_back(vault->id);
            return ids;
        }();
        const auto inferredRole = roleForScopeShorthand(scope);
        const auto existingDefaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id);
        const auto effectiveDefaultRoleId = existingDefaultRole && existingDefaultRole->enabled
            ? existingDefaultRole->vault_role_id
            : inferredRole->id;
        try {
            protocols::s3::CredentialManager::validateScopeMutation(
                call.user->id,
                credential->principal_user_id,
                "vault_allowlist",
                {scope},
                selectedVaultIds,
                effectiveDefaultRoleId);
        } catch (const std::exception& e) {
            return invalid(e.what());
        }
        db::query::s3::Gateway::updateCredentialScopeMode(
            credential->id,
            "vault_allowlist",
            credential->principal_user_id,
            credential->created_by,
            credential->description,
            credential->expires_at);
        if (!existingDefaultRole || !existingDefaultRole->enabled)
            db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
                credential->id,
                inferredRole->id,
                true,
                call.user->id);
        db::query::s3::Gateway::upsertCredentialSelectedVault(
            credential->id,
            vault->id,
            true,
            call.user->id);
        if (effectiveDefaultRoleId != inferredRole->id)
            db::query::s3::Gateway::upsertCredentialVaultRoleAssignment({
                .credential_id = credential->id,
                .vault_id = vault->id,
                .vault_role_id = inferredRole->id,
                .enabled = true,
                .created_by = call.user->id
            });
        else
            db::query::s3::Gateway::deleteCredentialVaultRoleAssignment(credential->id, vault->id);
        return ok("S3 gateway selected vault added from boolean shorthand. Gateway authorization uses vault roles.\n");
    }

    if (action == "revoke-vault") {
        const auto vaultValue = call.positionals.size() >= 3
            ? call.positionals[2]
            : optVal(call, "vault").value_or("");
        if (vaultValue.empty()) return invalid("s3-gateway creds scope revoke-vault: vault is required");
        const auto vault = resolveVaultArg(call, vaultValue);
        db::query::s3::Gateway::deleteCredentialVaultRoleAssignment(credential->id, vault->id);
        db::query::s3::Gateway::deleteCredentialSelectedVault(credential->id, vault->id);
        return ok("S3 gateway selected vault revoked.\n");
    }

    return invalid(call.constructFullArgs(), "Unknown s3-gateway creds scope action: '" + action + "'");
}

std::string renderCredentialRoleAssignments(const db::query::s3::GatewayCredential& credential) {
    std::ostringstream out;
    out << "S3 gateway credential vault roles\n";
    out << "  name: " << credential.name << "\n";
    out << "  access key: " << credential.access_key << "\n";
    out << "  principal: " << credential.principal_user_id << "\n";
    const auto assignments = db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    if (assignments.empty()) {
        out << "  roles: none\n";
        return out.str();
    }

    Table table({
        {"Vault", Align::Right, 2, 8, false, false},
        {"Vault Name", Align::Left, 6, 24, false, false},
        {"Role", Align::Right, 2, 8, false, false},
        {"Role Name", Align::Left, 6, 24, false, false},
        {"Enabled", Align::Left, 3, 7, false, false},
        {"Overrides", Align::Right, 1, 9, false, false}
    });
    for (const auto& assignment : assignments) {
        const auto vault = db::query::vault::Vault::getVault(assignment.vault_id);
        const auto role = db::query::rbac::role::Vault::get(assignment.vault_role_id);
        std::size_t overrideCount = 0;
        try {
            overrideCount = db::query::s3::Gateway::listCredentialVaultRoleOverrides(
                credential.id,
                assignment.vault_id).size();
        } catch (const std::exception&) {
        }
        table.add_row({
            std::to_string(assignment.vault_id),
            vault ? vault->name : "-",
            std::to_string(assignment.vault_role_id),
            role ? role->name : "-",
            yesNo(assignment.enabled),
            std::to_string(overrideCount)
        });
    }
    out << table.render();
    return out.str();
}

CommandResult handleCredsRoleList(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds role list: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role list: permission denied");

    const auto assignments = db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential->id);
    if (hasFlag(call, "json")) {
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& assignment : assignments) {
            const auto vault = db::query::vault::Vault::getVault(assignment.vault_id);
            const auto role = db::query::rbac::role::Vault::get(assignment.vault_role_id);
            rows.push_back({
                {"id", assignment.id},
                {"credential_id", assignment.credential_id},
                {"vault_id", assignment.vault_id},
                {"vault", vault ? nlohmann::json{{"id", vault->id}, {"name", vault->name}} : nlohmann::json(nullptr)},
                {"vault_role_id", assignment.vault_role_id},
                {"role", role ? nlohmann::json{{"id", role->id}, {"name", role->name}, {"description", role->description}} : nlohmann::json(nullptr)},
                {"enabled", assignment.enabled},
                {"created_by", assignment.created_by ? nlohmann::json(*assignment.created_by) : nlohmann::json(nullptr)},
                {"created_at", assignment.created_at},
                {"updated_at", assignment.updated_at}
            });
        }
        return ok(nlohmann::json{
            {"credential_id", credential->id},
            {"roles", rows}
        }.dump(4) + "\n");
    }
    return ok(renderCredentialRoleAssignments(*credential));
}

CommandResult handleCredsRoleAssign(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds role assign: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role assign: permission denied");

    const auto vaultValue = optVal(call, "vault").value_or("");
    const auto roleValue = optVal(call, "role").value_or("");
    if (vaultValue.empty()) return invalid("s3-gateway creds role assign: --vault is required");
    if (roleValue.empty()) return invalid("s3-gateway creds role assign: --role is required");

    const auto vault = resolveVaultArg(call, vaultValue);
    const auto role = resolveVaultRole(roleValue, "s3-gateway creds role assign");
    if (!role) return invalid(role.error);

    const auto scope = scopeFromVaultRole(credential->id, vault->id, role.ptr);
    if (const auto err = validateGatewayCredentialRoleMutation(call, *credential, scope)) return *err;
    if (const auto err = requireGatewayCredentialVaultRolePermission(
            call,
            *credential,
            vault->id,
            ::vh::rbac::permission::vault::RolePermissions::Assign,
            "assign")) return *err;

    if (credential->scope_mode == "global" && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role assign: admin.s3_gateway.manage_credentials is required for global credential exceptions");
    const auto nextScopeMode = credential->scope_mode == "global" ? std::string{"global"} : std::string{"vault_allowlist"};
    db::query::s3::Gateway::updateCredentialScopeMode(
        credential->id,
        nextScopeMode,
        credential->principal_user_id,
        credential->created_by,
        credential->description,
        credential->expires_at);
    if (!db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id)) {
        const auto implicitDeny = db::query::rbac::role::Vault::get("implicit_deny");
        db::query::s3::Gateway::upsertCredentialDefaultVaultRole(
            credential->id,
            implicitDeny ? implicitDeny->id : role.ptr->id,
            true,
            call.user->id);
    }
    if (nextScopeMode == "vault_allowlist")
        db::query::s3::Gateway::upsertCredentialSelectedVault(
            credential->id,
            vault->id,
            true,
            call.user->id);
    const auto assignmentId = db::query::s3::Gateway::upsertCredentialVaultRoleAssignment({
        .credential_id = credential->id,
        .vault_id = vault->id,
        .vault_role_id = role.ptr->id,
        .enabled = true,
        .created_by = call.user->id
    });

    if (hasFlag(call, "json")) {
        return ok(nlohmann::json{
            {"id", assignmentId},
            {"credential_id", credential->id},
            {"vault_id", vault->id},
            {"vault_role_id", role.ptr->id},
            {"role", {{"id", role.ptr->id}, {"name", role.ptr->name}}},
            {"vault", {{"id", vault->id}, {"name", vault->name}}}
        }.dump(4) + "\n");
    }
    return ok("Assigned role '" + role.ptr->name + "' to S3 gateway credential '" + credential->name +
              "' for vault '" + vault->name + "'.\n");
}

CommandResult handleCredsRoleRevoke(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds role revoke: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role revoke: permission denied");

    const auto vaultValue = optVal(call, "vault").value_or("");
    if (vaultValue.empty()) return invalid("s3-gateway creds role revoke: --vault is required");
    const auto vault = resolveVaultArg(call, vaultValue);

    if (const auto err = requireGatewayCredentialVaultRolePermission(
            call,
            *credential,
            vault->id,
            ::vh::rbac::permission::vault::RolePermissions::Revoke,
            "revoke")) return *err;

    const auto removedRole = db::query::s3::Gateway::deleteCredentialVaultRoleAssignment(credential->id, vault->id);
    if (!removedRole) return invalid("s3-gateway creds role revoke: assignment not found");
    return ok("Revoked S3 gateway credential role for vault '" + vault->name + "'.\n");
}

CommandResult handleCredsRoleOverrideList(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds role override list: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role override list: permission denied");

    const auto vaultValue = optVal(call, "vault").value_or("");
    if (vaultValue.empty()) return invalid("s3-gateway creds role override list: --vault is required");
    const auto vault = resolveVaultArg(call, vaultValue);
    if (const auto err = requireGatewayCredentialVaultRolePermission(
            call,
            *credential,
            vault->id,
            ::vh::rbac::permission::vault::RolePermissions::ViewOverride,
            "override list")) return *err;

    const auto overrides = db::query::s3::Gateway::getCredentialVaultRoleForVault(credential->id, vault->id)
        ? db::query::s3::Gateway::listCredentialVaultRoleOverrides(credential->id, vault->id)
        : std::vector<::vh::rbac::permission::Override>{};
    if (hasFlag(call, "json")) {
        nlohmann::json rows = overrides;
        return ok(nlohmann::json{
            {"credential_id", credential->id},
            {"vault_id", vault->id},
            {"overrides", rows}
        }.dump(4) + "\n");
    }
    return ok(::vh::rbac::permission::to_string(overrides) + "\n");
}

CommandResult handleCredsRoleOverrideAdd(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds role override add: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role override add: permission denied");

    const auto vaultValue = optVal(call, "vault").value_or("");
    if (vaultValue.empty()) return invalid("s3-gateway creds role override add: --vault is required");
    const auto vault = resolveVaultArg(call, vaultValue);

    if (const auto err = requireGatewayCredentialVaultRolePermission(
            call,
            *credential,
            vault->id,
            ::vh::rbac::permission::vault::RolePermissions::AssignOverride,
            "override add")) return *err;
    if (credential->scope_mode == "vault_allowlist") {
        const auto selectedVaults = db::query::s3::Gateway::listCredentialSelectedVaults(credential->id);
        const auto selected = std::ranges::any_of(selectedVaults, [&](const auto& selectedVault) {
            return selectedVault.vault_id == vault->id && selectedVault.enabled;
        });
        if (!selected) return invalid("s3-gateway creds role override add: vault is not selected for this credential");
    }
    if (!db::query::s3::Gateway::getCredentialVaultRoleForVault(credential->id, vault->id)) {
        const auto defaultRole = db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id);
        if (!defaultRole || !defaultRole->enabled)
            return invalid("s3-gateway creds role override add: default vault role is required first");
        db::query::s3::Gateway::upsertCredentialVaultRoleAssignment({
            .credential_id = credential->id,
            .vault_id = vault->id,
            .vault_role_id = defaultRole->vault_role_id,
            .enabled = true,
            .created_by = call.user->id
        });
    }

    const auto pattern = ::vh::protocols::shell::commands::vault::parseGlobPatternOpt(
        call,
        true,
        "s3-gateway creds role override add");
    if (!pattern.ok) return invalid(pattern.error);
    const auto enabled = ::vh::protocols::shell::commands::vault::parseEnableDisableOpt(
        call,
        "s3-gateway creds role override add");
    if (!enabled.ok) return invalid(enabled.error);

    ::vh::rbac::permission::Override overrideRule;
    try {
        overrideRule.permission = *resolveGatewayOverridePermission(call, "s3-gateway creds role override add");
        overrideRule.effect = resolveGatewayOverrideEffect(call, "s3-gateway creds role override add");
    } catch (const std::exception& e) {
        return invalid(e.what());
    }
    overrideRule.enabled = enabled.value.value_or(true);
    overrideRule.pattern = *pattern.pattern;

    const auto id = db::query::s3::Gateway::upsertCredentialVaultRoleOverride(
        credential->id,
        vault->id,
        overrideRule);
    if (hasFlag(call, "json")) {
        return ok(nlohmann::json{
            {"id", id},
            {"credential_id", credential->id},
            {"vault_id", vault->id},
            {"permission", overrideRule.permission.qualified_name},
            {"pattern", overrideRule.glob_path()},
            {"effect", ::vh::rbac::permission::to_string(overrideRule.effect)},
            {"enabled", overrideRule.enabled}
        }.dump(4) + "\n");
    }
    return ok("Added S3 gateway credential role override " + std::to_string(id) + ".\n");
}

CommandResult handleCredsRoleOverrideRemove(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway creds role override remove: credential not found");
    if (credential->principal_user_id != call.user->id && !canManageGatewayCredentials(call))
        return invalid("s3-gateway creds role override remove: permission denied");

    const auto vaultValue = optVal(call, "vault").value_or("");
    const auto overrideValue = optVal(call, "id").value_or(call.positionals.size() >= 2 ? call.positionals[1] : "");
    if (vaultValue.empty()) return invalid("s3-gateway creds role override remove: --vault is required");
    if (overrideValue.empty()) return invalid("s3-gateway creds role override remove: override id is required");
    const auto overrideId = parseUInt(overrideValue);
    if (!overrideId) return invalid("s3-gateway creds role override remove: override id must be a positive integer");

    const auto vault = resolveVaultArg(call, vaultValue);
    if (const auto err = requireGatewayCredentialVaultRolePermission(
            call,
            *credential,
            vault->id,
            ::vh::rbac::permission::vault::RolePermissions::RevokeOverride,
            "override remove")) return *err;

    if (!db::query::s3::Gateway::getCredentialVaultRoleForVault(credential->id, vault->id) ||
        !db::query::s3::Gateway::deleteCredentialVaultRoleOverride(credential->id, vault->id, *overrideId))
        return invalid("s3-gateway creds role override remove: override not found");
    return ok("Removed S3 gateway credential role override " + std::to_string(*overrideId) + ".\n");
}

CommandResult handleCredsRoleOverride(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "creds", "role", "override", "add"}, sub) || sub == "add")
        return handleCredsRoleOverrideAdd(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "role", "override", "list"}, sub) || sub == "list")
        return handleCredsRoleOverrideList(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "role", "override", "remove"}, sub) || sub == "remove")
        return handleCredsRoleOverrideRemove(subcall);
    return invalid(call.constructFullArgs(), "Unknown s3-gateway creds role override action: '" + std::string(sub) + "'");
}

CommandResult handleCredsRole(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "creds", "role", "assign"}, sub) || sub == "assign")
        return handleCredsRoleAssign(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "role", "revoke"}, sub) || sub == "revoke")
        return handleCredsRoleRevoke(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "role", "list"}, sub) || sub == "list")
        return handleCredsRoleList(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "role", "override"}, sub) || sub == "override")
        return handleCredsRoleOverride(subcall);
    return invalid(call.constructFullArgs(), "Unknown s3-gateway creds role subcommand: '" + std::string(sub) + "'");
}

CommandResult handleCreds(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "creds", "create"}, sub)) return handleCredsCreate(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "list"}, sub)) return handleCredsList(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "revoke"}, sub)) return handleCredsRevoke(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "scope"}, sub) || sub == "scope") return handleCredsScope(subcall);
    if (isCommandMatch({"s3-gateway", "creds", "role"}, sub) || sub == "role") return handleCredsRole(subcall);
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
    if (auto err = requireGatewayPermissionCommand(
            call,
            ::vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
            "bucket bind")) return *err;
    const auto vaultOpt = optVal(call, "vault");
    if (!vaultOpt || vaultOpt->empty()) return invalid("s3-gateway bucket bind: --vault is required");

    const auto vault = resolveVaultArg(call, *vaultOpt);
    requireVaultOwnerOrAdmin(call, vault);
    const auto mode = modeOrDefault(call, vault->type == ::vh::vault::model::VaultType::S3 ? "remote_cache" : "local");
    validateBucketModeForVault(vault, mode, "bucket bind");
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
    if (auto err = requireGatewayPermissionCommand(
            call,
            ::vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
            "bucket unbind")) return *err;
    const auto binding = db::query::s3::Gateway::resolveBucket(call.positionals[0]);
    if (!binding) return invalid("s3-gateway bucket unbind: bucket binding not found");
    const auto vault = db::query::vault::Vault::getVault(binding->vault_id);
    requireVaultOwnerOrAdmin(call, vault);
    if (!db::query::s3::Gateway::unbindBucket(call.positionals[0]))
        return invalid("s3-gateway bucket unbind: bucket binding not found");
    return ok("Unbound bucket " + call.positionals[0] + ".\n");
}

CommandResult handleBucketCreateLocal(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    auto owner = resolveTargetUser(call);
    if (owner->id != call.user->id && !canManageGatewayBuckets(call))
        return invalid("s3-gateway bucket create-local: admin.s3_gateway.manage_buckets is required for another owner");
    uintmax_t quota = 0;
    if (const auto quotaOpt = optVal(call, "quota"); quotaOpt && !quotaOpt->empty()) {
        ::vh::vault::model::Vault quotaParser;
        quotaParser.setQuotaFromStr(*quotaOpt);
        quota = quotaParser.quota;
    }
    protocols::s3::ObjectStore store;
    const auto bucket = store.createBucket(call.positionals[0], call.user, owner->id, "local", quota);
    return ok("Created local S3 gateway bucket " + bucket.bucket_name + " on vault " + std::to_string(bucket.vault_id) + ".\n");
}

CommandResult handleBucketCreateRemoteCache(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    if (!canManageGatewayBuckets(call))
        return invalid("s3-gateway bucket create-remote-cache: admin.s3_gateway.manage_buckets is required");
    if (!canCreateVaultForOwner(call, call.user->id))
        return invalid("s3-gateway bucket create-remote-cache: vault create permission is required");

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

CommandResult handleBucketBackfill(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    if (auto err = requireGatewayPermissionCommand(
            call,
            ::vh::rbac::permission::admin::S3GatewayPermissions::ManageBuckets,
            "bucket backfill")) return *err;
    const auto bucketName = call.positionals[0];
    const auto binding = db::query::s3::Gateway::resolveBucket(bucketName);
    if (!binding) return invalid("s3-gateway bucket backfill: bucket binding not found");
    const auto vault = db::query::vault::Vault::getVault(binding->vault_id);
    requireVaultOwnerOrAdmin(call, vault);

    const bool calculateEtags = hasFlag(call, "calculate-etags");
    std::uint64_t calculated = 0;
    if (binding->mode == "remote_cache" || binding->mode == "remote_proxy") {
        db::query::s3::Gateway::backfillObjectStateFromRemoteIndex(binding->vault_id);
    } else {
        db::query::s3::Gateway::backfillObjectStateFromFs(binding->vault_id);
    }

    if (calculateEtags) {
        if (binding->mode != "local")
            return invalid("s3-gateway bucket backfill: --calculate-etags is only supported for local Vaulthalla files");
        auto engine = runtime::Deps::get().storageManager->getEngine(binding->vault_id);
        if (!engine) return invalid("s3-gateway bucket backfill: storage engine not available");
        const auto files = db::query::fs::File::listFilesInDir(binding->vault_id, "/", true);
        for (const auto& file : files) {
            if (!file) continue;
            const auto objectKey = gatewayObjectKeyFromPath(file->path);
            if (objectKey.empty()) continue;
            const auto plaintext = plaintextForGatewayBackfill(engine, file);
            db::query::s3::Gateway::upsertObject({
                .vault_id = binding->vault_id,
                .object_key = objectKey,
                .etag = md5EtagForGatewayBackfill(plaintext),
                .size_bytes = file->size_bytes,
                .content_type = file->mime_type,
                .storage_class = file->remote_storage_class,
                .last_modified = file->updated_at,
                .multipart = false,
                .part_count = std::nullopt
            });
            ++calculated;
        }
    }

    std::ostringstream out;
    out << "Backfilled S3 gateway metadata for bucket " << bucketName << ".\n";
    if (calculateEtags) {
        out << "WARNING: --calculate-etags read/decrypted local object bodies intentionally.\n";
        out << "Calculated plaintext MD5 ETags for " << calculated << " objects.\n";
    } else {
        out << "Metadata-only backfill completed; no object bodies were read or decrypted.\n";
    }
    return ok(out.str());
}

CommandResult handleBucket(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "bucket", "list"}, sub) || sub == "list" || sub == "ls")
        return handleBucketList(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "bind"}, sub) || sub == "bind") return handleBucketBind(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "unbind"}, sub) || sub == "unbind") return handleBucketUnbind(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "create-local"}, sub) || sub == "create-local")
        return handleBucketCreateLocal(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "create-remote-cache"}, sub) || sub == "create-remote-cache")
        return handleBucketCreateRemoteCache(subcall);
    if (isCommandMatch({"s3-gateway", "bucket", "backfill"}, sub) || sub == "backfill") return handleBucketBackfill(subcall);
    return invalid(call.constructFullArgs(), "Unknown s3-gateway bucket subcommand: '" + std::string(sub) + "'");
}

std::optional<std::uint32_t> optionalCredentialFilter(const CommandCall& call, const bool allowAnyCredential) {
    const auto keyOpt = optVal(call, "key");
    if (!keyOpt || keyOpt->empty()) return std::nullopt;
    const auto credential = resolveCredentialForGatewayBudget(call, *keyOpt, allowAnyCredential);
    if (!credential) throw std::runtime_error("s3-gateway budget: credential not found");
    return credential->id;
}

std::optional<std::uint32_t> optionalVaultFilter(const CommandCall& call) {
    const auto vaultOpt = optVal(call, "vault");
    if (!vaultOpt || vaultOpt->empty()) return std::nullopt;
    return resolveVaultArg(call, *vaultOpt)->id;
}

void requireCanViewBudgetFilters(
    const CommandCall& call,
    const std::optional<std::uint32_t>& credentialId,
    const std::optional<std::uint32_t>& vaultId) {
    if (canViewGatewayBudgets(call)) return;
    if (vaultId) {
        auto vault = db::query::vault::Vault::getVault(*vaultId);
        requireVaultOwnerOrAdmin(call, vault);
        return;
    }
    if (credentialId) return;
    throw std::runtime_error("s3-gateway budget: non-admin users must pass --key or --vault");
}

std::vector<storage::s3::pricing::PriceBudgetPolicy> gatewayBudgetPoliciesForCall(
    const CommandCall& call,
    const std::optional<std::uint32_t>& credentialId,
    const std::optional<std::uint32_t>& vaultId) {
    auto policies = storage::s3::pricing::PriceBudgetService{}.listPolicies(true);
    std::erase_if(policies, [&](const auto& policy) {
        if (policy.scope != storage::s3::pricing::PriceBudgetScope::GatewayCredential &&
            policy.scope != storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault)
            return true;
        if (credentialId && policy.gateway_credential_id != credentialId) return true;
        if (vaultId) {
            if (policy.scope == storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault &&
                policy.vault_id != vaultId)
                return true;
            if (policy.scope == storage::s3::pricing::PriceBudgetScope::GatewayCredential && !credentialId)
                return true;
        }
        if (!canViewGatewayBudgets(call) && policy.gateway_credential_id) {
            const auto ownsCredential = callerOwnsCredential(call, *policy.gateway_credential_id);
            if (policy.scope == storage::s3::pricing::PriceBudgetScope::GatewayCredential && !ownsCredential)
                return true;
            if (!vaultId && credentialId && !ownsCredential)
                return true;
        }
        return false;
    });
    return policies;
}

void filterGatewayBudgetLedger(std::vector<storage::s3::pricing::PriceBudgetLedgerEntry>& ledger) {
    std::erase_if(ledger, [](const auto& entry) {
        return !entry.gateway_credential_id;
    });
}

void filterGatewayBudgetTrends(std::vector<storage::s3::pricing::PriceBudgetTrendStats>& trends) {
    std::erase_if(trends, [](const auto& trend) {
        return trend.scope != "gateway_credential" &&
            trend.scope != "gateway_credential_vault";
    });
}

storage::s3::pricing::PriceBudgetPolicy gatewayBudgetPolicyFromCall(
    const CommandCall& call,
    const storage::s3::pricing::PriceBudgetScope scope,
    const uint32_t credentialId,
    const std::optional<uint32_t> vaultId) {
    const auto monthly = optVal(call, "monthly");
    if (!monthly || monthly->empty() || !storage::s3::pricing::isValidPriceBudgetDecimal(*monthly))
        throw std::runtime_error("s3-gateway budget: --monthly must be a non-negative decimal with at most 8 fractional digits");
    storage::s3::pricing::PriceBudgetPolicy policy;
    policy.scope = scope;
    policy.gateway_credential_id = credentialId;
    policy.vault_id = vaultId;
    policy.mode = storage::s3::pricing::PriceBudgetMode::Enforce;
    if (const auto mode = optVal(call, "mode"))
        policy.mode = storage::s3::pricing::priceBudgetModeFromString(*mode);
    policy.currency = storage::s3::pricing::normalizePriceBudgetCurrency(optVal(call, "currency").value_or("USD"));
    policy.max_monthly_cost = *monthly;
    policy.require_verified_catalog = !hasFlag(call, "no-require-verified-catalog");
    policy.allow_stale_catalog = hasFlag(call, "allow-stale-catalog");
    policy.max_catalog_age_seconds = 43200;
    return policy;
}

CommandResult handleBudgetSetKey(const CommandCall& call) {
    if (!canManageGatewayBudgets(call))
        return invalid("s3-gateway budget set-key: admin.s3_gateway.manage_budgets is required for key-only budgets");
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway budget set-key: credential not found");
    auto policy = gatewayBudgetPolicyFromCall(
        call,
        storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        credential->id,
        std::nullopt);
    const auto saved = storage::s3::pricing::PriceBudgetService{}.upsertPolicy(policy);
    return ok("S3 gateway key budget saved.\n" + renderGatewayBudgetPolicies({saved}));
}

CommandResult handleBudgetSetKeyVault(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    if (!canManageGatewayBudgets(call))
        return invalid("s3-gateway budget set-key-vault: admin.s3_gateway.manage_budgets is required");
    const auto vaultOpt = optVal(call, "vault");
    if (!vaultOpt || vaultOpt->empty()) return invalid("s3-gateway budget set-key-vault: --vault is required");
    const auto vault = resolveVaultArg(call, *vaultOpt);
    requireVaultOwnerOrAdmin(call, vault);
    const auto credential = resolveCredentialForGatewayBudget(call, call.positionals[0], true);
    if (!credential) return invalid("s3-gateway budget set-key-vault: credential not found");
    auto policy = gatewayBudgetPolicyFromCall(
        call,
        storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credential->id,
        vault->id);
    const auto saved = storage::s3::pricing::PriceBudgetService{}.upsertPolicy(policy);
    return ok("S3 gateway key/vault budget saved.\n" + renderGatewayBudgetPolicies({saved}));
}

CommandResult handleGatewayBudgetList(const CommandCall& call) {
    const auto vaultId = optionalVaultFilter(call);
    if (!canViewGatewayBudgets(call) && vaultId) {
        auto vault = db::query::vault::Vault::getVault(*vaultId);
        requireVaultOwnerOrAdmin(call, vault);
    }
    const auto credentialId = optionalCredentialFilter(call, canViewGatewayBudgets(call) || vaultId.has_value());
    requireCanViewBudgetFilters(call, credentialId, vaultId);
    auto policies = gatewayBudgetPoliciesForCall(call, credentialId, vaultId);
    if (hasFlag(call, "json")) return ok(nlohmann::json(policies).dump(4) + "\n");
    return ok(renderGatewayBudgetPolicies(policies));
}

CommandResult handleBudgetDisableKey(const CommandCall& call) {
    if (!canManageGatewayBudgets(call))
        return invalid("s3-gateway budget disable-key: admin.s3_gateway.manage_budgets is required for key-only budgets");
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    const auto credential = resolveCredentialForCaller(call, call.positionals[0]);
    if (!credential) return invalid("s3-gateway budget disable-key: credential not found");
    const auto disabled = storage::s3::pricing::PriceBudgetService{}.disablePolicy(
        storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        std::nullopt,
        std::nullopt,
        credential->id);
    return ok(disabled ? "S3 gateway key budget disabled.\n" : "No matching S3 gateway key budget was configured.\n");
}

CommandResult handleBudgetDisableKeyVault(const CommandCall& call) {
    if (call.positionals.empty()) return usage(call.constructFullArgs());
    if (!canManageGatewayBudgets(call))
        return invalid("s3-gateway budget disable-key-vault: admin.s3_gateway.manage_budgets is required");
    const auto vaultOpt = optVal(call, "vault");
    if (!vaultOpt || vaultOpt->empty()) return invalid("s3-gateway budget disable-key-vault: --vault is required");
    const auto vault = resolveVaultArg(call, *vaultOpt);
    requireVaultOwnerOrAdmin(call, vault);
    const auto credential = resolveCredentialForGatewayBudget(call, call.positionals[0], true);
    if (!credential) return invalid("s3-gateway budget disable-key-vault: credential not found");
    const auto disabled = storage::s3::pricing::PriceBudgetService{}.disablePolicy(
        storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        std::nullopt,
        vault->id,
        credential->id);
    return ok(disabled ? "S3 gateway key/vault budget disabled.\n" : "No matching S3 gateway key/vault budget was configured.\n");
}

CommandResult handleBudgetLedger(const CommandCall& call) {
    auto limit = std::uint32_t{50};
    if (const auto limitOpt = optVal(call, "limit")) {
        const auto parsed = parseUInt(*limitOpt);
        if (!parsed || *parsed == 0) return invalid("s3-gateway budget ledger: --limit must be a positive integer");
        limit = *parsed;
    }
    const auto vaultId = optionalVaultFilter(call);
    if (!canViewGatewayBudgets(call) && vaultId) {
        auto vault = db::query::vault::Vault::getVault(*vaultId);
        requireVaultOwnerOrAdmin(call, vault);
    }
    const auto credentialId = optionalCredentialFilter(call, canViewGatewayBudgets(call) || vaultId.has_value());
    if (!canViewGatewayBudgets(call) && !vaultId && !credentialId)
        return invalid("s3-gateway budget ledger: non-admin users must pass --key or --vault");
    auto ledger = storage::s3::pricing::PriceBudgetService{}.listLedger(
        limit,
        vaultId,
        credentialId);
    filterGatewayBudgetLedger(ledger);
    if (hasFlag(call, "json")) return ok(nlohmann::json(ledger).dump(4) + "\n");
    if (ledger.empty()) return ok("No S3 gateway budget ledger rows.\n");
    std::ostringstream out;
    for (const auto& entry : ledger) {
        out << "- id=" << entry.id
            << " policy=" << entry.policy_id
            << " key=" << gwValueOrDash(entry.gateway_credential_id)
            << " vault=" << entry.vault_id
            << " op=" << gwValueOrDash(entry.operation)
            << " reserved=" << entry.reserved_cost
            << " committed=" << gwValueOrDash(entry.committed_cost)
            << " " << entry.currency
            << " status=" << entry.status;
        if (entry.synthetic) out << " synthetic=true";
        if (entry.usage_source) out << " source=" << *entry.usage_source;
        out << "\n";
    }
    return ok(out.str());
}

CommandResult handleBudgetStatus(const CommandCall& call) {
    storage::s3::pricing::PriceBudgetService service;
    service.expireStaleReservations();
    auto limit = std::uint32_t{50};
    if (const auto limitOpt = optVal(call, "limit")) {
        const auto parsed = parseUInt(*limitOpt);
        if (!parsed || *parsed == 0) return invalid("s3-gateway budget status: --limit must be a positive integer");
        limit = *parsed;
    }
    const auto vaultId = optionalVaultFilter(call);
    if (!canViewGatewayBudgets(call) && vaultId) {
        auto vault = db::query::vault::Vault::getVault(*vaultId);
        requireVaultOwnerOrAdmin(call, vault);
    }
    const auto credentialId = optionalCredentialFilter(call, canViewGatewayBudgets(call) || vaultId.has_value());
    requireCanViewBudgetFilters(call, credentialId, vaultId);

    const auto policies = gatewayBudgetPoliciesForCall(call, credentialId, vaultId);
    auto ledger = service.listLedger(limit, vaultId, credentialId);
    filterGatewayBudgetLedger(ledger);
    auto trends = service.trendStats(vaultId, credentialId);
    filterGatewayBudgetTrends(trends);
    if (hasFlag(call, "json")) {
        return ok(nlohmann::json{
            {"policies", policies},
            {"ledger", ledger},
            {"trends", trends}
        }.dump(4) + "\n");
    }

    std::ostringstream out;
    out << "S3 gateway budgets\n" << renderGatewayBudgetPolicies(policies)
        << "\nCurrent usage\n" << renderGatewayBudgetTrends(trends)
        << "\nRecent ledger rows\n";
    if (ledger.empty()) {
        out << "No S3 gateway budget ledger rows.\n";
    } else {
        for (const auto& entry : ledger) {
            out << "- id=" << entry.id
                << " policy=" << entry.policy_id
                << " key=" << gwValueOrDash(entry.gateway_credential_id)
                << " vault=" << entry.vault_id
                << " op=" << gwValueOrDash(entry.operation)
                << " reserved=" << entry.reserved_cost
                << " committed=" << gwValueOrDash(entry.committed_cost)
                << " " << entry.currency
                << " status=" << entry.status;
            if (entry.synthetic) out << " synthetic=true";
            if (entry.usage_source) out << " source=" << *entry.usage_source;
            out << "\n";
        }
    }
    return ok(out.str());
}

CommandResult handleGatewayBudget(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"s3-gateway", "budget", "set-key"}, sub) || sub == "set-key") return handleBudgetSetKey(subcall);
    if (isCommandMatch({"s3-gateway", "budget", "set-key-vault"}, sub) || sub == "set-key-vault") return handleBudgetSetKeyVault(subcall);
    if (isCommandMatch({"s3-gateway", "budget", "list"}, sub) || sub == "list") return handleGatewayBudgetList(subcall);
    if (isCommandMatch({"s3-gateway", "budget", "disable-key"}, sub) || sub == "disable-key") return handleBudgetDisableKey(subcall);
    if (isCommandMatch({"s3-gateway", "budget", "disable-key-vault"}, sub) || sub == "disable-key-vault") return handleBudgetDisableKeyVault(subcall);
    if (isCommandMatch({"s3-gateway", "budget", "ledger"}, sub) || sub == "ledger") return handleBudgetLedger(subcall);
    if (isCommandMatch({"s3-gateway", "budget", "status"}, sub) || sub == "status") return handleBudgetStatus(subcall);
    return invalid(call.constructFullArgs(), "Unknown s3-gateway budget subcommand: '" + std::string(sub) + "'");
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
        if (isGatewayMatch("budget", sub) || sub == "budget") return handleGatewayBudget(subcall);
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
