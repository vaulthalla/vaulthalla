#include "protocols/ws/handler/vault/APIKeys.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "vault/APIKeyManager.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/permission/Admin.hpp"
#include "rbac/vault/resolver/*.hpp"
#include "rbac/permission/vault/Roles.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::rbac;
using json = nlohmann::json;

using Permission = permission::vault::APIKeyPermissions;

json APIKeys::add(const json& payload, const std::shared_ptr<Session>& session) {
    const auto name = payload.at("name").get<std::string>();
    const auto provider = s3_provider_from_string(payload.at("provider").get<std::string>());
    const auto accessKey = payload.at("access_key").get<std::string>();
    const auto secretKey = payload.at("secret_access_key").get<std::string>();
    const auto region = payload.at("region").get<std::string>();
    const auto endpoint = payload.at("endpoint").get<std::string>();

    if (!vh::rbac::vault::Resolver::has<Permission>({
        .user = session->user,
        .permission = Permission::Modify
    })) throw std::runtime_error("Insufficient permissions to add API key");

    auto key = std::make_shared<APIKey>(session->user->id, name, provider, accessKey, secretKey, region, endpoint);
    runtime::Deps::get().apiKeyManager->addAPIKey(key);

    return {};
}

json APIKeys::remove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto keyId = payload.at("id").get<unsigned int>();

    if (!vh::rbac::vault::Resolver::has<Permission>({
        .user = session->user,
        .permission = Permission::Modify
    })) throw std::runtime_error("Insufficient permissions to remove API key");

    runtime::Deps::get().apiKeyManager->removeAPIKey(keyId, session->user->id);
    return {};
}

json APIKeys::list(const std::shared_ptr<Session>& session) {
    const bool canView = vh::rbac::vault::Resolver::has<Permission>({
        .user = session->user,
        .permission = Permission::View
    });

    const auto keys = canView ?
        runtime::Deps::get().apiKeyManager->listAPIKeys() :
        runtime::Deps::get().apiKeyManager->listUserAPIKeys(session->user->id);

    return {{"keys", json(keys).dump(4)}};
}

json APIKeys::get(const json& payload, const std::shared_ptr<Session>& session) {
    // TODO: implement a context policy in resolver for API key
    if (!vh::rbac::vault::Resolver::has<Permission>({
        .user = session->user,
        .permission = Permission::View
    })) throw std::runtime_error("Insufficient permissions to get API key");

    const unsigned int keyId = payload.at("id").get<unsigned int>();
    return {{"api_key", runtime::Deps::get().apiKeyManager->getAPIKey(keyId, session->user->id)}};
}
