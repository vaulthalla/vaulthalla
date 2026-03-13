#include "rbac/permission/vault/sync/Config.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault::sync {

std::string Config::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Config:\n";
    const std::string in(indent + 2, ' ');
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "Edit: " << bool_to_string(canEdit()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const Config& cfg) {
    j = {
        {"view", cfg.canView()},
        {"edit", cfg.canEdit()}
    };
}

void from_json(const nlohmann::json& j, Config& cfg) {
    cfg.clear();
    if (j.at("view").get<bool>()) cfg.grant(SyncConfigPermissions::View);
    if (j.at("edit").get<bool>()) cfg.grant(SyncConfigPermissions::Edit);
}

}
