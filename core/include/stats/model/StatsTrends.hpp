#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model {

struct StatsTrendPoint {
    std::uint64_t createdAt = 0;
    double value = 0.0;
};

struct StatsTrendSeries {
    std::string key;
    std::string label;
    std::string unit;
    std::string snapshotType;
    std::vector<StatsTrendPoint> points;
};

struct StatsTrends {
    std::string scope = "system";
    std::optional<std::uint32_t> vaultId;
    std::uint32_t windowHours = 168;
    std::vector<StatsTrendSeries> series;
    std::uint64_t checkedAt = 0;
};

void to_json(nlohmann::json& j, const StatsTrendPoint& point);
void to_json(nlohmann::json& j, const StatsTrendSeries& series);
void to_json(nlohmann::json& j, const StatsTrends& trends);

}
