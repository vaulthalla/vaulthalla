#include "stats/model/StatsTrends.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableStatsTrendValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void to_json(nlohmann::json& j, const StatsTrendPoint& point) {
    j = nlohmann::json{
        {"created_at", point.createdAt},
        {"value", point.value},
    };
}

void to_json(nlohmann::json& j, const StatsTrendSeries& series) {
    j = nlohmann::json{
        {"key", series.key},
        {"label", series.label},
        {"unit", series.unit},
        {"snapshot_type", series.snapshotType},
        {"points", series.points},
    };
}

void to_json(nlohmann::json& j, const StatsTrends& trends) {
    j = nlohmann::json{
        {"scope", trends.scope},
        {"vault_id", nullableStatsTrendValue(trends.vaultId)},
        {"window_hours", trends.windowHours},
        {"series", trends.series},
        {"checked_at", trends.checkedAt},
    };
}

}
