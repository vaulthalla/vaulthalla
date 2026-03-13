#include "rbac/permission/vault/sync/Action.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault::sync {

void to_json(nlohmann::json& j, const Action& a) {
    j = {{"trigger", a.canTrigger()}};
}

void from_json(const nlohmann::json& j, Action& a) {
    a.clear();
    if (j.at("trigger").get<bool>()) a.grant(SyncActionPermissions::Trigger);
}

}
