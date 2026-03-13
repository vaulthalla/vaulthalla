#include "rbac/permission/vault/Sync.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault {

void to_json(nlohmann::json& j, const Sync& v) {
    j = {
        {"config", v.config},
        {"action", v.action}
    };
}

void from_json(const nlohmann::json& j, Sync& v) {
    j.at("config").get_to(v.config);
    j.at("action").get_to(v.action);
}

}
