#include "rbac/role/vault/Base.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/result>
#include <ostream>

namespace vh::rbac::role::vault {
    Base::Base(const pqxx::row &row)
        : roles(static_cast<permission::vault::Roles::Mask>(
              row["roles_permissions"].as<uint64_t>())),
          sync(static_cast<permission::vault::Sync::Mask>(
              row["sync_permissions"].as<uint64_t>())),
          fs(row) {
    }

    Base::Base(const pqxx::row &row, const pqxx::result &overrides)
        : roles(static_cast<permission::vault::Roles::Mask>(
              row["roles_permissions"].as<uint32_t>())),
          sync(static_cast<permission::vault::Sync::Mask>(
              row["sync_permissions"].as<uint32_t>())),
          fs(row, overrides) {
    }

    std::string Base::toFlagsString() const {
        std::ostringstream oss;
        oss << roles.toFlagsString()
                << sync.toFlagsString()
                << fs.toFlagString();
        return oss.str();
    }

    std::string Base::toString(const uint8_t indent) const {
        std::ostringstream oss;
        oss << std::string(indent, ' ') << "Vault Permissions:\n";
        const auto i = indent + 2;
        oss << roles.toString(i);
        oss << sync.toString(i);
        oss << fs.toString(i);
        return oss.str();
    }

    void to_json(nlohmann::json &j, const Base &v) {
        j = {
            {"roles", v.roles},
            {"sync", v.sync},
            {"filesystem", v.fs},
        };
    }

    void from_json(const nlohmann::json &j, Base &v) {
        j.at("roles").get_to(v.roles);
        j.at("sync").get_to(v.sync);
        j.at("filesystem").get_to(v.fs);
    }
}
