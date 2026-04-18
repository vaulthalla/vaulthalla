#include "rbac/permission/Override.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "db/encoding/has.hpp"
#include "rbac/fs/glob/Tokenizer.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

using namespace vh::rbac::permission;
using namespace vh::protocols::shell;
using namespace vh::db::encoding;

namespace {
    [[nodiscard]] std::string lowerCopy(std::string value) {
        std::ranges::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }
}

Override::Override(const pqxx::row &row)
    : assignment_id(row["assignment_id"].as<unsigned int>()),
      permission(row),
      effect(overrideOptFromString(row["effect"].as<std::string>())),
      enabled(row["enabled"].as<bool>()),
      pattern(rbac::fs::glob::Tokenizer::parse(row["glob_path"].as<std::string>())) {
    if (const auto v = try_get<uint32_t>(row, std::vector<std::string_view>{"permission_override_id", "override_id", "id"}))
        id = *v;

    if (const auto v = try_get<uint32_t>(row, std::vector<std::string_view>{"role_assignment_id", "assignment_id"}))
        assignment_id = *v;
}

Override::Override(const nlohmann::json &j)
    : id(j.value("id", 0u)),
      assignment_id(j.value("assignment_id", 0u)),
      permission(j.at("permission").get<Permission>()),
      effect(overrideOptFromString(j.value("effect", std::string("allow")))),
      enabled(j.value("enabled", true)),
      pattern(j.value("glob_path", std::string())) {
}

std::string vh::rbac::permission::to_string(const OverrideOpt &opt) {
    switch (opt) {
        case OverrideOpt::ALLOW: return "allow";
        case OverrideOpt::DENY: return "deny";
    }

    throw std::invalid_argument("Unhandled OverrideOpt enum value");
}

OverrideOpt vh::rbac::permission::overrideOptFromString(const std::string &str) {
    const auto lwr = lowerCopy(str);

    if (lwr == "allow") return OverrideOpt::ALLOW;
    if (lwr == "deny") return OverrideOpt::DENY;

    throw std::invalid_argument("Invalid OverrideOpt string: " + str);
}

void vh::rbac::permission::to_json(nlohmann::json &j, const Override &po) {
    j = {
        {"id", po.id},
        {"permission", po.permission},
        {"effect", to_string(po.effect)},
        {"assignment_id", po.assignment_id},
        {"enabled", po.enabled},
        {"glob_path", po.glob_path()}
    };
}

void vh::rbac::permission::from_json(const nlohmann::json &j, Override &po) {
    if (!j.contains("permission"))
        throw std::runtime_error("Override JSON must contain 'permission'");

    po.id = j.value("id", 0u);
    po.permission = j.at("permission").get<Permission>();
    po.effect = overrideOptFromString(j.value("effect", std::string("allow")));
    po.assignment_id = j.value("assignment_id", 0u);
    po.enabled = j.value("enabled", true);
    po.pattern = fs::glob::model::Pattern(j.value("glob_path", std::string()));
}

std::vector<Override> vh::rbac::permission::permissionOverridesFromPqRes(const pqxx::result &res) {
    std::vector<Override> overrides;
    overrides.reserve(res.size());

    for (const auto &row: res) overrides.emplace_back(row);

    return overrides;
}

std::vector<Override> vh::rbac::permission::permissionOverridesFromJson(const nlohmann::json &j) {
    if (!j.is_array())
        throw std::runtime_error("permissionOverridesFromJson expected a JSON array");

    std::vector<Override> overrides;
    overrides.reserve(j.size());

    for (const auto &item: j) overrides.emplace_back(item);

    return overrides;
}

void vh::rbac::permission::to_json(nlohmann::json &j, const std::vector<Override> &overrides) {
    j = nlohmann::json::array();
    for (const auto &override: overrides) j.emplace_back(override);
}

std::string vh::rbac::permission::to_string(const Override &override) {
    std::ostringstream ss;

    ss << "ID: " << override.id << "\n"
            << "Permission: " << override.permission.qualified_name << " (" << override.permission.description << ")\n"
            << "Glob Path: " << override.glob_path() << "\n"
            << "Effect: " << to_string(override.effect) << "\n"
            << "Enabled: " << (override.enabled ? "On" : "Off") << "\n"
            << "Assignment ID: " << override.assignment_id << "\n";

    return ss.str();
}

std::string vh::rbac::permission::to_string(const std::vector<Override> &overrides) {
    if (overrides.empty()) return "No overrides";

    Table tbl({
                  {"ID", Align::Left, 4, 8, false, false},
                  {"NAME", Align::Left, 4, 32, false, false},
                  {"DESCRIPTION", Align::Left, 4, 64, false, false},
                  {"GLOB PATH", Align::Left, 4, 64, false, false},
                  {"EFFECT", Align::Left, 4, 8, false, false},
                  {"ENABLED", Align::Left, 4, 8, false, false}
              }, term_width());

    for (const auto &ovr: overrides) {
        tbl.add_row({
            std::to_string(ovr.id),
            ovr.permission.qualified_name,
            ovr.permission.description,
            ovr.glob_path(),
            to_string(ovr.effect),
            ovr.enabled ? "On" : "Off"
        });
    }

    return tbl.render();
}
