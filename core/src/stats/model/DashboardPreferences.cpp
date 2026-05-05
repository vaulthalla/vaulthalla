#include "stats/model/DashboardPreferences.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json dashboardPreferenceNullable(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void to_json(nlohmann::json& j, const DashboardPreference& preference) {
    j = nlohmann::json{
        {"id", dashboardPreferenceNullable(preference.id)},
        {"user_id", preference.userId},
        {"preference_key", preference.preferenceKey},
        {"layout", preference.layout.is_null() ? nlohmann::json::object() : preference.layout},
        {"created_at", dashboardPreferenceNullable(preference.createdAt)},
        {"updated_at", dashboardPreferenceNullable(preference.updatedAt)},
        {"exists", preference.exists},
    };
}

}
