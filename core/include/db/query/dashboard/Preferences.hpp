#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vh::stats::model { struct DashboardPreference; }

namespace vh::db::query::dashboard {

struct Preferences {
    static std::shared_ptr<::vh::stats::model::DashboardPreference> getForUser(
        std::uint32_t userId,
        const std::string& preferenceKey = "dashboard.home"
    );

    static std::shared_ptr<::vh::stats::model::DashboardPreference> upsertForUser(
        std::uint32_t userId,
        const std::string& preferenceKey,
        const nlohmann::json& layout
    );

    static bool resetForUser(
        std::uint32_t userId,
        const std::string& preferenceKey = "dashboard.home"
    );
};

}
