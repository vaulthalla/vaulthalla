#include "rbac/permission/admin/Audits.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin {

void to_json(nlohmann::json& j, const Audits& a) {
    j = {{"view", a.canView()}};
}

void from_json(const nlohmann::json& j, Audits& a) {
    a.permissions = 0;
    if (j.at("view").get<bool>()) a.permissions |= static_cast<Audits::Mask>(AuditPermissions::View);
}

}
