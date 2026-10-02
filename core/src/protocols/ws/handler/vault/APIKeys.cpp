#include "protocols/ws/handler/vault/APIKeys.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "protocols/ws/Session.hpp"
#include "ops/APIKeys.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;
using json = nlohmann::json;

json APIKeys::add(const json& payload, const std::shared_ptr<Session>& session) {
    std::optional<vh::ops::api_keys::Ref> owner;
    if (payload.contains("owner_id") && !payload.at("owner_id").is_null()) owner = payload.at("owner_id").get<uint32_t>();

    const auto key = vh::ops::api_keys::create(session->user, {
        .name = payload.at("name").get<std::string>(),
        .provider = vh::vault::model::s3_provider_from_string(payload.at("provider").get<std::string>()),
        .access_key = payload.at("access_key").get<std::string>(),
        .secret_access_key = payload.at("secret_access_key").get<std::string>(),
        .endpoint = payload.at("endpoint").get<std::string>(),
        .region = payload.value("region", "auto"),
        .owner = owner
    });
    return {{"api_key", key}};
}

json APIKeys::remove(const json& payload, const std::shared_ptr<Session>& session) {
    (void)vh::ops::api_keys::remove(session->user, payload.at("id").get<unsigned int>());
    return {};
}

json APIKeys::list(const std::shared_ptr<Session>& session) {
    // The console parses `keys` as a JSON string (WebSocketCommandMap types it `string`).
    return {{"keys", json(vh::ops::api_keys::list(session->user)).dump(4)}};
}

json APIKeys::get(const json& payload, const std::shared_ptr<Session>& session) {
    return {{"api_key", vh::ops::api_keys::get(session->user, payload.at("id").get<unsigned int>())}};
}
