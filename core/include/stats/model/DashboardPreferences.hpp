#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace vh::stats::model {

struct DashboardPreference {
    std::optional<std::uint32_t> id;
    std::uint32_t userId = 0;
    std::string preferenceKey = "dashboard.home";
    nlohmann::json layout;
    std::optional<std::uint64_t> createdAt;
    std::optional<std::uint64_t> updatedAt;
    bool exists = false;
};

void to_json(nlohmann::json& j, const DashboardPreference& preference);

}
