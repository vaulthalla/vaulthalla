#include "rbac/permission/admin/Audits.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin {
    std::string Audits::toString(const uint8_t indent) const {
        std::ostringstream oss;
        oss << std::string(indent, ' ') << "Audits:\n";
        const auto in = std::string(indent + 2, ' ');
        oss << in << "View: " << bool_to_string(canView()) << "\n";
        return oss.str();
    }

    std::string Audits::toFlagsString() const {
        return joinFlagsWithOwn();
    }

    ModuleSet<unsigned char, AuditPermissions, unsigned char>::Mask Audits::toMask() const { return packWithOwn(); }
    void Audits::fromMask(const Mask mask) { unpackWithOwn(mask); }

    void to_json(nlohmann::json &j, const Audits &a) {
        j = {{"view", a.canView()}};
    }

    void from_json(const nlohmann::json &j, Audits &a) {
        a.clear();
        if (j.at("view").get<bool>()) a.grant(AuditPermissions::View);
    }
}
