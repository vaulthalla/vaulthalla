#pragma once

#include "Permission.hpp"
#include "rbac/fs/glob/model/Pattern.hpp"

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <nlohmann/json_fwd.hpp>

namespace pqxx {
    class row;
    class result;
}

namespace vh::rbac::permission {
    enum class OverrideOpt {
        ALLOW,
        DENY
    };

    std::string to_string(const OverrideOpt &opt);

    OverrideOpt overrideOptFromString(const std::string &str);

    struct Override {
        uint32_t id{}, assignment_id{};
        Permission permission;
        OverrideOpt effect{OverrideOpt::ALLOW};
        bool enabled{true};
        fs::glob::model::Pattern pattern;

        Override() = default;

        explicit Override(const pqxx::row &row);

        explicit Override(const nlohmann::json &j);

        [[nodiscard]] std::string glob_path() const { return pattern.source; }
    };

    void to_json(nlohmann::json &j, const Override &po);

    void from_json(const nlohmann::json &j, Override &po);

    std::vector<Override> permissionOverridesFromPqRes(const pqxx::result &res);

    std::vector<Override> permissionOverridesFromJson(const nlohmann::json &j);

    void to_json(nlohmann::json &j, const std::vector<Override> &overrides);

    std::string to_string(const Override &override);

    std::string to_string(const std::vector<Override> &overrides);
}
