#include "protocols/ws/handler/vault/Vaults.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/vault/APIKey.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "sync/Controller.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "rbac/permission/admin/Keys.hpp"
#include "rbac/permission/admin/Vaults.hpp"

#include <nlohmann/json.hpp>
#include <boost/algorithm/string.hpp>

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::sync::model;
using namespace vh::rbac;
using json = nlohmann::json;

json Vaults::add(const json &payload, const std::shared_ptr<Session> &session) {
    const std::string name = payload.at("name").get<std::string>();
    const std::string type = payload.at("type").get<std::string>();
    const std::string typeLower = boost::algorithm::to_lower_copy(type);
    const std::string mountPoint = payload.at("mount_point").get<std::string>();
    const auto ownerId = payload.contains("owner_id")
                             ? std::make_optional(payload.at("owner_id").get<uint32_t>())
                             : session->user->id;

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::Create,
        .target_user_id = ownerId
    })) throw std::runtime_error("User does not have permission to add vault.");

    std::shared_ptr<Vault> vault;
    std::shared_ptr<Policy> sync = nullptr;

    if (typeLower == "s3") {
        const auto apiKeyID = payload.at("api_key_id").get<unsigned int>();

        if (!resolver::Admin::has<permission::admin::keys::APIPermissions>({
            .user = session->user,
            .permission = permission::admin::keys::APIPermissions::Consume,
            .api_key_id = apiKeyID
        })) throw std::runtime_error("User does not have permission to add this api-key to vault.");

        const std::string bucket = payload.at("bucket").get<std::string>();
        const auto s3Vault = std::make_shared<S3Vault>(name, apiKeyID, bucket);
        const auto apiKey = db::query::vault::APIKey::getAPIKey(apiKeyID);
        if (!apiKey) throw std::runtime_error("API key not found: " + std::to_string(apiKeyID));

        const auto requestedTier = payload.contains("storage_tier_id") && !payload.at("storage_tier_id").is_null()
            ? std::make_optional(payload.at("storage_tier_id").get<std::string>())
            : std::optional<std::string>{};
        const auto tier = storage::s3::provider::resolve(apiKey->provider)->normalizeStorageTier(requestedTier);
        if (!tier.ok) throw std::runtime_error(tier.error);
        s3Vault->storage_tier_id = tier.normalized_id;

        vault = s3Vault;
        sync = std::make_shared<RemotePolicy>(payload);
    }

    vault->name = name;
    vault->mount_point = mountPoint;
    vault->owner_id = session->user->id;

    vault = runtime::Deps::get().storageManager->addVault(vault, sync);

    return {{"vault", *vault}};
}

json Vaults::update(const json &payload, const std::shared_ptr<Session> &session) {
    std::shared_ptr<Vault> vault;
    const auto type = from_string(payload.at("type").get<std::string>());
    if (type == VaultType::S3) {
        const auto s3Vault = std::make_shared<S3Vault>();
        from_json(payload, *s3Vault);
        const auto apiKey = db::query::vault::APIKey::getAPIKey(s3Vault->api_key_id);
        if (!apiKey) throw std::runtime_error("API key not found: " + std::to_string(s3Vault->api_key_id));

        const auto tier = storage::s3::provider::resolve(apiKey->provider)->normalizeStorageTier(s3Vault->storage_tier_id);
        if (!tier.ok) throw std::runtime_error(tier.error);
        s3Vault->storage_tier_id = tier.normalized_id;
        vault = s3Vault;
    } else {
        vault = std::make_shared<Vault>();
        from_json(payload, *vault);
    }

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::Edit,
        .vault_id = vault->id
    })) throw std::runtime_error("User does not have permission to update vault.");

    // TODO: pull a diff and apply per role vGlobal perms to changes

    runtime::Deps::get().storageManager->updateVault(vault);
    return {{"vault", *vault}};
}

json Vaults::remove(const json &payload, const std::shared_ptr<Session> &session) {
    const auto vaultId = payload.at("id").get<unsigned int>();

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::Remove,
        .vault_id = vaultId
    }))
        throw std::runtime_error("User does not have permission to remove vault.");

    const auto vault = runtime::Deps::get().storageManager->getVault(vaultId);
    if (!vault) throw std::runtime_error("Vault not found with ID: " + std::to_string(vaultId));

    runtime::Deps::get().storageManager->removeVault(vaultId);
    return {};
}

json Vaults::get(const json &payload, const std::shared_ptr<Session> &session) {
    const auto vaultId = payload.at("id").get<unsigned int>();

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::View,
        .vault_id = vaultId
    }))
        throw std::runtime_error("User does not have permission to view vault.");

    const auto vault = runtime::Deps::get().storageManager->getVault(vaultId);
    if (!vault) throw std::runtime_error("Vault not found with ID: " + std::to_string(vaultId));

    json data = {};

    if (vault->type == VaultType::S3) {
        const auto s3Vault = std::static_pointer_cast<S3Vault>(vault);
        data["vault"] = *s3Vault;
    } else data["vault"] = *vault;

    if (vault->owner_id == session->user->id) data["vault"]["owner"] = session->user->name;
    else data["vault"]["owner"] = db::query::vault::Vault::getVaultOwnersName(vaultId);

    return data;
}

json Vaults::list(const std::shared_ptr<Session> &session) {
    const auto &adminVPerms = session->user->vaultsPerms();
    if (adminVPerms.self.canView() && !(adminVPerms.admin.canView() || adminVPerms.user.canView()))
        return json{{"vaults", db::query::vault::Vault::listUserVaults(session->user->id)}};

    auto vaults = db::query::vault::Vault::listVaults();
    std::erase_if(vaults, [&](const auto &v) {
            return !resolver::Admin::has<permission::admin::VaultPermissions>({
                .user = session->user,
                .permission = permission::admin::VaultPermissions::View,
                .vault_id = v->id
            });
        });

    return json{{"vaults", vaults}};
}

json Vaults::sync(const json &payload, const std::shared_ptr<Session> &session) {
    const auto vaultId = payload.at("id").get<unsigned int>();

    if (!resolver::Vault::has<permission::vault::sync::SyncActionPermissions>({
        .user = session->user,
        .permission = permission::vault::sync::SyncActionPermissions::Trigger,
        .vault_id = vaultId
    })) throw std::runtime_error("User does not have permission to trigger vault.");

    runtime::Deps::get().syncController->runNow(vaultId);
    return {};
}
