#include "protocols/ws/handler/dashboard/Preferences.hpp"

#include "db/query/dashboard/Preferences.hpp"
#include "identities/User.hpp"
#include "protocols/ws/Session.hpp"
#include "stats/model/DashboardPreferences.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace vh::protocols::ws::handler::dashboard {

namespace {

constexpr const char* kDefaultPreferenceKey = "dashboard.home";

std::uint32_t currentUserId(const std::shared_ptr<Session>& session) {
    if (!session || !session->user) throw std::runtime_error("Unauthorized");
    if (session->user->id == 0) throw std::runtime_error("Unauthorized");
    return session->user->id;
}

std::string preferenceKeyFromPayload(const json& payload) {
    if (!payload.is_object()) return kDefaultPreferenceKey;
    return payload.value("preference_key", std::string{kDefaultPreferenceKey});
}

}

json Preferences::get(const json& payload, const std::shared_ptr<Session>& session) {
    const auto preferences = vh::db::query::dashboard::Preferences::getForUser(
        currentUserId(session),
        preferenceKeyFromPayload(payload)
    );

    return {{"preferences", preferences ? json(*preferences) : json(nullptr)}};
}

json Preferences::update(const json& payload, const std::shared_ptr<Session>& session) {
    if (!payload.is_object()) throw std::invalid_argument("Dashboard preference update payload must be an object.");
    if (!payload.contains("layout")) throw std::invalid_argument("Dashboard preference update requires a layout.");

    const auto preferences = vh::db::query::dashboard::Preferences::upsertForUser(
        currentUserId(session),
        preferenceKeyFromPayload(payload),
        payload.at("layout")
    );

    return {{"preferences", preferences ? json(*preferences) : json(nullptr)}};
}

json Preferences::reset(const json& payload, const std::shared_ptr<Session>& session) {
    const auto reset = vh::db::query::dashboard::Preferences::resetForUser(
        currentUserId(session),
        preferenceKeyFromPayload(payload)
    );

    return {{"reset", true}, {"deleted", reset}};
}

}
