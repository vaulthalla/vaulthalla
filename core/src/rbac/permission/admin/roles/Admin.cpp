#include "rbac/permission/admin/roles/Admin.hpp"

#include <nlohmann/json.hpp>

namespace vh::rbac::permission::admin::roles {

    std::string Admin::toString(const uint8_t indent) const {
        std::string result = std::string(indent, ' ') + "Admin:\n";
        result += Base::toString(indent + 2);
        return result;
    }

    void to_json(nlohmann::json &j, const Admin &admin) {
        j = {{ "admin", static_cast<const Base&>(admin) }};
    }

    void from_json(const nlohmann::json& j, Admin& admin) {
        j.at("admin").get_to(static_cast<Base&>(admin));
    }

}
