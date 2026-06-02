#include "protocols/s3/CredentialManager.hpp"

#include "crypto/secrets/TPMKeyProvider.hpp"
#include "crypto/util/encrypt.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/s3/Gateway.hpp"
#include "rbac/permission/admin/S3Gateway.hpp"
#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"

#include <algorithm>
#include <array>
#include <paths.h>
#include <sodium.h>
#include <stdexcept>

namespace vh::protocols::s3 {

namespace {
constexpr std::string_view kAccessAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr std::string_view kSecretAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/+";

std::string randomFromAlphabet(const std::string_view alphabet, const std::size_t length) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed");
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i)
        out.push_back(alphabet[randombytes_uniform(alphabet.size())]);
    return out;
}

bool validScopeMode(const std::string& mode) {
    return mode == "user_access" || mode == "global" || mode == "vault_allowlist";
}

bool actionAllowedByPrincipal(
    const std::shared_ptr<identities::User>& user,
    const uint32_t vaultId,
    const rbac::permission::vault::FilesystemAction action) {
    if (!user) return false;
    if (user->isSuperAdmin()) return true;
    return rbac::resolver::Vault::has<rbac::permission::vault::FilesystemAction>({
        .user = user,
        .permission = action,
        .vault_id = vaultId,
        .path = "/"
    });
}

bool canAssignGatewayPrincipal(const std::shared_ptr<identities::User>& actor) {
    if (!actor) return false;
    if (actor->isSuperAdmin()) return true;
    using Perm = rbac::permission::admin::S3GatewayPermissions;
    return rbac::resolver::Admin::has<Perm>({
        .user = actor,
        .permission = Perm::AssignPrincipal
    });
}

void validateScopeRequest(const CredentialCreateOptions& options) {
    if (options.created_by == 0) throw std::invalid_argument("credential creation requires created_by");
    if (options.principal_user_id == 0) throw std::invalid_argument("credential creation requires principal_user_id");
    if (options.name.empty()) throw std::invalid_argument("credential name must not be empty");
    CredentialManager::validateScopeMutation(
        options.created_by,
        options.principal_user_id,
        options.scope_mode,
        options.vault_scopes);
}
}

CredentialManager::CredentialManager()
    : tpmKeyProvider_(std::make_unique<crypto::secrets::TPMKeyProvider>(
          paths::testMode ? "test_s3gw_master" : "s3gw_master")) {
    tpmKeyProvider_->init();
}

GatewaySecret CredentialManager::createCredential(const uint32_t userId, const std::string& name) const {
    return createCredential({
        .created_by = userId,
        .principal_user_id = userId,
        .name = name,
        .scope_mode = "user_access",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {},
        .enforce_budget_for_local_requests = false
    });
}

GatewaySecret CredentialManager::createCredential(const CredentialCreateOptions& options) const {
    validateScopeRequest(options);

    GatewaySecret out;
    out.secret_access_key = generateSecretKey();

    std::vector<uint8_t> iv;
    out.credential.user_id = options.principal_user_id;
    out.credential.created_by = options.created_by;
    out.credential.principal_user_id = options.principal_user_id;
    out.credential.name = options.name;
    out.credential.access_key = generateAccessKey();
    out.credential.encrypted_secret_access_key = encryptSecret(out.secret_access_key, iv);
    out.credential.iv = std::move(iv);
    out.credential.enabled = true;
    out.credential.enforce_budget_for_local_requests = options.enforce_budget_for_local_requests;
    out.credential.scope_mode = options.scope_mode;
    out.credential.description = options.description;
    out.credential.expires_at = options.expires_at;
    out.credential.id = db::query::s3::Gateway::createCredential(out.credential);
    if (out.credential.scope_mode == "vault_allowlist") {
        auto scopes = options.vault_scopes;
        for (auto& scope : scopes) scope.credential_id = out.credential.id;
        db::query::s3::Gateway::replaceCredentialScopes(out.credential.id, scopes);
    } else {
        db::query::s3::Gateway::replaceCredentialScopes(out.credential.id, {});
    }
    return out;
}

std::vector<db::query::s3::GatewayCredential> CredentialManager::listCredentials(const std::optional<uint32_t> userId) const {
    return db::query::s3::Gateway::listCredentials(userId);
}

bool CredentialManager::revokeCredential(const std::string& accessKeyOrName, const std::optional<uint32_t> userId) const {
    if (accessKeyOrName.starts_with("VH"))
        return db::query::s3::Gateway::deleteCredentialByAccessKey(accessKeyOrName);
    if (!userId) return false;
    return db::query::s3::Gateway::deleteCredentialByName(*userId, accessKeyOrName);
}

std::optional<GatewaySecret> CredentialManager::findEnabledSecret(const std::string& accessKey) const {
    const auto credential = db::query::s3::Gateway::getCredentialByAccessKey(accessKey);
    if (!credential || !credential->enabled) return std::nullopt;
    if (credential->expires_at && *credential->expires_at <= std::time(nullptr)) return std::nullopt;
    return GatewaySecret{
        .credential = *credential,
        .secret_access_key = decryptSecret(credential->encrypted_secret_access_key, credential->iv)
    };
}

void CredentialManager::markUsed(const uint32_t credentialId) const {
    db::query::s3::Gateway::updateCredentialLastUsed(credentialId);
}

std::string CredentialManager::generateAccessKey() {
    return "VH" + randomFromAlphabet(kAccessAlphabet, 24);
}

std::string CredentialManager::generateSecretKey() {
    return randomFromAlphabet(kSecretAlphabet, 40);
}

void CredentialManager::validateScopeMutation(
    const uint32_t actorUserId,
    const uint32_t principalUserId,
    const std::string& scopeMode,
    const std::vector<CredentialVaultScope>& vaultScopes) {
    if (actorUserId == 0) throw std::invalid_argument("S3 gateway credential scope update requires an actor user");
    if (principalUserId == 0) throw std::invalid_argument("S3 gateway credential scope update requires a principal user");
    if (!validScopeMode(scopeMode)) throw std::invalid_argument("invalid S3 gateway credential scope mode: " + scopeMode);

    const auto actor = db::query::identities::User::getUserById(actorUserId);
    const auto principal = db::query::identities::User::getUserById(principalUserId);
    if (!actor || !actor->meta.is_active) throw std::invalid_argument("credential scope actor is not active");
    if (!principal || !principal->meta.is_active) throw std::invalid_argument("credential principal is not active");

    const bool actorAdmin = actor->isAdmin();
    if (principalUserId != actorUserId && !canAssignGatewayPrincipal(actor))
        throw std::invalid_argument("assigning an S3 gateway credential to another principal requires admin.s3_gateway.assign_principal");
    if (!actorAdmin && scopeMode == "global")
        throw std::invalid_argument("global S3 gateway credentials require admin permission");
    if (scopeMode == "global" && !principal->isAdmin())
        throw std::invalid_argument("global S3 gateway credentials require an admin principal");
    if (scopeMode != "vault_allowlist") return;

    if (!actorAdmin) {
        for (const auto& scope : vaultScopes) {
            if (scope.can_admin)
                throw std::invalid_argument("non-admin users cannot grant S3 gateway admin scope");
            const auto hasAnyVaultAccess =
                actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::List) ||
                actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::Read) ||
                actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::Write) ||
                actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::Delete);
            if (!hasAnyVaultAccess)
                throw std::invalid_argument("principal cannot access vault " + std::to_string(scope.vault_id));
            if (scope.can_list && !actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::List))
                throw std::invalid_argument("principal cannot list vault " + std::to_string(scope.vault_id));
            if (scope.can_read && !actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::Read))
                throw std::invalid_argument("principal cannot read vault " + std::to_string(scope.vault_id));
            if (scope.can_write && !actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::Write))
                throw std::invalid_argument("principal cannot write vault " + std::to_string(scope.vault_id));
            if (scope.can_delete && !actionAllowedByPrincipal(principal, scope.vault_id, rbac::permission::vault::FilesystemAction::Delete))
                throw std::invalid_argument("principal cannot delete from vault " + std::to_string(scope.vault_id));
        }
    }
}

std::vector<uint8_t> CredentialManager::encryptSecret(const std::string& secret, std::vector<uint8_t>& iv) const {
    const auto masterKey = tpmKeyProvider_->getMasterKey();
    const auto plaintext = std::vector<uint8_t>(secret.begin(), secret.end());
    return crypto::util::encrypt_aes256_gcm(plaintext, masterKey, iv);
}

std::string CredentialManager::decryptSecret(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv) const {
    const auto masterKey = tpmKeyProvider_->getMasterKey();
    const auto decrypted = crypto::util::decrypt_aes256_gcm(ciphertext, masterKey, iv);
    return {decrypted.begin(), decrypted.end()};
}

} // namespace vh::protocols::s3
