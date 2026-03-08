#include "protocols/ws/handler/APIKeys.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/model/User.hpp"
#include "vault/APIKeyManager.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "log/Registry.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::storage;
using json = nlohmann::json;

json APIKeys::add(const json& payload, const std::shared_ptr<Session>& session) {
    const auto userID = payload.at("user_id").get<unsigned int>();
    if (session->user->id != userID) throw std::invalid_argument("Invalid user ID");

    const auto name = payload.at("name").get<std::string>();
    const auto provider = s3_provider_from_string(payload.at("provider").get<std::string>());
    const auto accessKey = payload.at("access_key").get<std::string>();
    const auto secretKey = payload.at("secret_access_key").get<std::string>();
    const auto region = payload.at("region").get<std::string>();
    const auto endpoint = payload.at("endpoint").get<std::string>();

    auto key = std::make_shared<APIKey>(userID, name, provider, accessKey, secretKey, region, endpoint);
    runtime::Deps::get().apiKeyManager->addAPIKey(key);

    return {};
}

json APIKeys::remove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto keyId = payload.at("id").get<unsigned int>();
    runtime::Deps::get().apiKeyManager->removeAPIKey(keyId, session->user->id);
    return {};
}

json APIKeys::list(const std::shared_ptr<Session>& session) {

    const auto keys = session->user->canManageAPIKeys() ?
        runtime::Deps::get().apiKeyManager->listAPIKeys() :
        runtime::Deps::get().apiKeyManager->listUserAPIKeys(session->user->id);

    return {{"keys", json(keys).dump(4)}};
}

json APIKeys::get(const json& payload, const std::shared_ptr<Session>& session) {
    const unsigned int keyId = payload.at("id").get<unsigned int>();
    return {{"api_key", runtime::Deps::get().apiKeyManager->getAPIKey(keyId, session->user->id)}};
}
