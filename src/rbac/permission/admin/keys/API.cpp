#include "rbac/permission/admin/keys/API.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin::keys {
    std::string APIKeyBase::toString(const uint8_t indent) const {
        std::ostringstream oss;
        const std::string in(indent, ' ');
        oss << in << "View: " << bool_to_string(canView()) << "\n";
        oss << in << "Create: " << bool_to_string(canCreate()) << "\n";
        oss << in << "Edit: " << bool_to_string(canEdit()) << "\n";
        oss << in << "Remove: " << bool_to_string(canRemove()) << "\n";
        oss << in << "Export: " << bool_to_string(canExport()) << "\n";
        oss << in << "Rotate: " << bool_to_string(canRotate()) << "\n";
        oss << in << "Consume: " << bool_to_string(canConsume()) << "\n";
        return oss.str();
    }

    std::string APIKeys::toString(const uint8_t indent) const {
        std::ostringstream oss;
        const std::string in(indent, ' ');
        oss << in << "API Key Self Permissions\n" << self.toString(indent + 2);
        oss << in << "API Key Admin Permissions\n" << admin.toString(indent + 2);
        oss << in << "API Key User Permissions\n" << user.toString(indent + 2);
        return oss.str();
    }

    std::string APIKeys::toFlagsString() const {
        std::ostringstream oss;
        oss << self.toFlagsString() << " " << admin.toFlagsString() << " " << user.toFlagsString();
        return oss.str();
    }

    void to_json(nlohmann::json &j, const APIKeyBase &p) {
        j = {
            {"view", p.canView()},
            {"create", p.canCreate()},
            {"edit", p.canEdit()},
            {"remove", p.canRemove()},
            {"export", p.canExport()},
            {"rotate", p.canRotate()},
            {"consume", p.canConsume()}
        };
    }

    void from_json(const nlohmann::json &j, APIKeyBase &p) {
        p.clear();
        if (j.at("view").get<bool>()) p.grant(APIPermissions::View);
        if (j.at("create").get<bool>()) p.grant(APIPermissions::Create);
        if (j.at("edit").get<bool>()) p.grant(APIPermissions::Edit);
        if (j.at("remove").get<bool>()) p.grant(APIPermissions::Remove);
        if (j.at("export").get<bool>()) p.grant(APIPermissions::Export);
        if (j.at("rotate").get<bool>()) p.grant(APIPermissions::Rotate);
        if (j.at("consume").get<bool>()) p.grant(APIPermissions::Consume);
    }

    void to_json(nlohmann::json &j, const APIKeys &p) {
        j = {
            {"self", p.self},
            {"admin", p.admin},
            {"user", p.user}
        };
    }

    void from_json(const nlohmann::json &j, APIKeys &p) {
        j.at("self").get_to(p.self);
        j.at("admin").get_to(p.admin);
        j.at("user").get_to(p.user);
    }
}
