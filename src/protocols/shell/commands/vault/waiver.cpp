#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"

#include "db/query/vault/APIKey.hpp"
#include "db/query/identities/User.hpp"

#include "storage/s3/Controller.hpp"
#include "vault/APIKeyManager.hpp"

#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/model/APIKey.hpp"
#include "rbac/role/Vault.hpp"
#include "identities/User.hpp"
#include "sync/model/Waiver.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/resolver/vault/all.hpp"

#include "config/Registry.hpp"
#include "vault/terms/waiver.hpp"

#include <string>
#include <string_view>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::config;
using namespace vh::crypto;
using namespace vh::sync::model;
using namespace vh::vault::terms;

static std::shared_ptr<Waiver> create_encrypt_waiver(const CommandCall& call, const std::shared_ptr<S3Vault>& s3Vault) {
    auto waiver = std::make_shared<Waiver>();
    waiver->vault = s3Vault;
    waiver->user = call.user;
    waiver->apiKey = db::query::vault::APIKey::getAPIKey(s3Vault->api_key_id);
    waiver->encrypt_upstream = s3Vault->encrypt_upstream;
    waiver->waiver_text = s3Vault->encrypt_upstream ?
        ENABLE_UPSTREAM_ENCRYPTION_WAIVER : DISABLE_UPSTREAM_ENCRYPTION_WAIVER;

    using Perm = rbac::permission::vault::sync::SyncActionPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::SignWaiver,
        .vault_id = s3Vault->id
    })) throw std::runtime_error("Failed to get permissions for vault");

    return waiver;
}

static bool upstream_bucket_is_empty(const std::shared_ptr<S3Vault>& s3Vault) {
    const auto apiKey = runtime::Deps::get().apiKeyManager->getAPIKey(s3Vault->api_key_id, s3Vault->owner_id);
    if (!apiKey) throw std::runtime_error("Failed to load API key ID " + std::to_string(s3Vault->api_key_id));
    if (apiKey->secret_access_key.empty()) throw std::runtime_error("API key ID " + std::to_string(s3Vault->api_key_id) + " has no secret access key");
    const s3::Controller ctrl(apiKey, s3Vault->bucket);

    if (const auto [ok, msg] = ctrl.validateAPICredentials(); !ok)
        throw std::runtime_error("Failed to validate S3 credentials: " + msg);

    return ctrl.isBucketEmpty();
}

static bool requires_waiver(const CommandCall& call, const std::shared_ptr<S3Vault>& s3Vault, const bool isUpdate = false) {
    const bool hasEncryptFlag = hasFlag(call, "encrypt");
    const bool hasNoEncryptFlag = hasFlag(call, "no-encrypt");

    if (hasEncryptFlag && hasNoEncryptFlag)
        throw std::runtime_error("Cannot use --encrypt and --no-encrypt together");

    if (!hasEncryptFlag && !hasNoEncryptFlag) {
        if (isUpdate) return false;
        s3Vault->encrypt_upstream = true;
        return !upstream_bucket_is_empty(s3Vault);
    }

    if (hasEncryptFlag) {
        s3Vault->encrypt_upstream = true;
        if (isUpdate && s3Vault->encrypt_upstream) return false;
        if (hasFlag(call, "accept-overwrite-waiver")) return false;
        return !upstream_bucket_is_empty(s3Vault);
    }

    // hasNoEncryptFlag
    s3Vault->encrypt_upstream = false;
    if (isUpdate && !s3Vault->encrypt_upstream) return false;
    if (hasFlag(call, "accept_decryption_waiver")) return false;
    return !upstream_bucket_is_empty(s3Vault);
}

WaiverResult commands::vault::handle_encryption_waiver(const WaiverContext& ctx) {
    if (!ctx.vault) throw std::invalid_argument("Invalid vault");
    if (ctx.vault->type != VaultType::S3) return {true, nullptr};

    const auto s3Vault = std::static_pointer_cast<S3Vault>(ctx.vault);
    if (!s3Vault) throw std::runtime_error("Failed to cast vault to S3Vault");

    if (!requires_waiver(ctx.call, s3Vault, ctx.isUpdate)) return {true, nullptr};

    const auto waiverText = s3Vault->encrypt_upstream ?
        ENABLE_UPSTREAM_ENCRYPTION_WAIVER : DISABLE_UPSTREAM_ENCRYPTION_WAIVER;

    if (const auto res = ctx.call.io->prompt(waiverText, "I DO NOT ACCEPT"); res == "I ACCEPT") {
        const auto waiver = create_encrypt_waiver(ctx.call, s3Vault);
        if (!waiver) throw std::runtime_error("Failed to create encryption waiver");
        return {true, waiver};
    }

    return {false, nullptr};
}
