#include "protocols/ws/handler/vault/APIKeys.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "vault/APIKeyManager.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/resolver/admin/all.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::rbac;
using json = nlohmann::json;

using Permission = permission::admin::keys::APIPermissions;

json APIKeys::add(const json& payload, const std::shared_ptr<Session>& session) {
    const auto name = payload.at("name").get<std::string>();
    const auto provider = s3_provider_from_string(payload.at("provider").get<std::string>());
    const auto accessKey = payload.at("access_key").get<std::string>();
    const auto secretKey = payload.at("secret_access_key").get<std::string>();
    const auto region = payload.at("region").get<std::string>();
    const auto endpoint = payload.at("endpoint").get<std::string>();
    const auto ownerId = payload.contains("owner_id") ? payload.at("owner_id").get<uint32_t>() : session->user->id;

    if (!resolver::Admin::has<Permission>({
        .user = session->user,
        .permission = Permission::Create,
        .target_user_id = ownerId
    })) throw std::runtime_error("Insufficient permissions to create API key");

    auto key = std::make_shared<APIKey>(session->user->id, name, provider, accessKey, secretKey, region, endpoint);
    runtime::Deps::get().apiKeyManager->addAPIKey(key);

    return {};
}

json APIKeys::remove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto keyId = payload.at("id").get<unsigned int>();

    if (!resolver::Admin::has<Permission>({
        .user = session->user,
        .permission = Permission::Remove,
        .api_key_id = keyId
    })) throw std::runtime_error("Insufficient permissions to remove API key");

    runtime::Deps::get().apiKeyManager->removeAPIKey(keyId, session->user->id);
    return {};
}

json APIKeys::list(const std::shared_ptr<Session>& session) {
    const auto& akPerms = session->user->apiKeysPerms();
    if (akPerms.self.canView() && !(akPerms.admin.canView() || akPerms.user.canView()))
        return {{"keys", json(runtime::Deps::get().apiKeyManager->listUserAPIKeys(session->user->id)).dump(4)}};

    auto keys = runtime::Deps::get().apiKeyManager->listAPIKeys();
    for (const auto& key : keys) {
        if (!resolver::Admin::has<Permission>({
            .user = session->user,
            .permission = Permission::View,
            .api_key_id = key->id
        })) std::erase(keys, key);
    }

    return {{"keys", json(keys).dump(4)}};
}

json APIKeys::get(const json& payload, const std::shared_ptr<Session>& session) {
    const unsigned int keyId = payload.at("id").get<unsigned int>();

    if (!resolver::Admin::has<Permission>({
            .user = session->user,
            .permission = Permission::View,
            .api_key_id = keyId
        })) throw std::runtime_error("Insufficient permissions to get API key");

    return {{"api_key", runtime::Deps::get().apiKeyManager->getAPIKey(keyId, session->user->id)}};
}
