#include "protocols/ws/handler/Settings.hpp"
#include "identities/User.hpp"
#include "protocols/ws/Session.hpp"
#include "config/Registry.hpp"
#include "config/Config.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;

json Settings::get(const std::shared_ptr<Session>& session) {
    // TODO: need to make settings granular by permissions
    if (!session->user->isSuperAdmin()) throw std::runtime_error("Permission denied: Only admins can view settings");
    return {{"settings", config::Registry::get()}};
}

json Settings::update(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->isSuperAdmin()) throw std::runtime_error("Permission denied: Only admins can update settings");

    const config::Config config(payload);
    config.save();
    return {{"settings", config}};
}
