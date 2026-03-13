#include "rbac/permission/vault/sync/Config.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault::sync {

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
