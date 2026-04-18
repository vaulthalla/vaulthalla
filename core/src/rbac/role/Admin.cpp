#include "rbac/role/Admin.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/has.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "usages.hpp"
#include "rbac/resolver/permission/all.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using namespace vh::db::encoding;
using namespace vh::protocols::shell;

namespace vh::rbac::role {
    Admin::Admin(const pqxx::row &row, const pqxx::result &globalVaultRoles)
        : Meta(row),
          identities(static_cast<typename decltype(identities)::Mask>(
              row["identity_permissions"].as<uint64_t>())),
          vaults(row["vaults_permissions"].as<typename decltype(vaults)::Mask>()),
          audits(static_cast<typename decltype(audits)::Mask>(row["audit_permissions"].as<uint64_t>())),
          settings(static_cast<typename decltype(settings)::Mask>(
              row["settings_permissions"].as<uint64_t>())),
          roles(static_cast<typename decltype(roles)::Mask>(
              row["roles_permissions"].as<uint64_t>())),
          keys(static_cast<typename decltype(keys)::Mask>(
              row["keys_permissions"].as<uint64_t>())),
          vGlobals(globalVaultRoles) {
        if (const auto id = try_get<uint32_t>(row, "user_id")) user_id = *id;
    }

    Admin::Admin(const pqxx::row &row)
        : Meta(row),
          identities(static_cast<typename decltype(identities)::Mask>(
              row["identity_permissions"].as<uint64_t>())),
          vaults(row["vaults_permissions"].as<typename decltype(vaults)::Mask>()),
          audits(static_cast<typename decltype(audits)::Mask>(row["audit_permissions"].as<uint64_t>())),
          settings(static_cast<typename decltype(settings)::Mask>(
              row["settings_permissions"].as<uint64_t>())),
          roles(static_cast<typename decltype(roles)::Mask>(
              row["roles_permissions"].as<uint64_t>())),
          keys(static_cast<typename decltype(keys)::Mask>(
              row["keys_permissions"].as<uint64_t>())) {
        if (const auto id = try_get<uint32_t>(row, "user_id")) user_id = *id;
    }

    Admin::Admin(const nlohmann::json &j)
        : Meta(j) {
        if (j.contains("user_id")) user_id = j["user_id"].get<uint32_t>();
        if (!j.contains("permissions") || !j["permissions"].is_array()) return;

        std::unordered_map<std::string, bool> pMap;
        for (const auto& p : j.at("permissions"))
            pMap.emplace(p.at("qualified").get<std::string>(), p.at("value").get<bool>());

        using PermResolver = resolver::PermissionResolverEnumPack<std::shared_ptr<Admin>>::type;
        auto self = shared_from_this();
        PermResolver::applyPermissionsFromWebCli(self, toPermissions(), pMap);
    }

    void Admin::updateFromJson(const nlohmann::json &j) {
        Meta::updateFromJson(j);
        if (j.contains("user_id")) user_id = j["user_id"].get<uint32_t>();

        if (!j.contains("permissions") || !j["permissions"].is_array()) return;

        std::unordered_map<std::string, bool> pMap;
        for (const auto& p : j.at("permissions"))
            pMap.emplace(p.at("qualified").get<std::string>(), p.at("value").get<bool>());

        using PermResolver = resolver::PermissionResolverEnumPack<std::shared_ptr<Admin>>::type;
        auto self = shared_from_this();
        PermResolver::applyPermissionsFromWebCli(self, toPermissions(), pMap);
    }

    std::string Admin::usage() {
        return formatPermissionTable(
            rbac::role::Admin::None().getFlags(),
            "Permission Flags:",
            "You can use either the --manage-* shorthand to set, or explicitly use --set/--unset."
        );
    }

    std::unordered_map<std::string, permission::Permission> Admin::toFlagMap() const {
        std::unordered_map<std::string, permission::Permission> flagMap;

        for (const auto &perm: toPermissions())
            for (const auto& flag : perm.flags)
                flagMap[flag] = perm;

        return flagMap;
    }

    std::vector<std::string> Admin::getFlags() const {
        auto identitiesFlags = identities.getFlags();
        auto vaultsFlags = vaults.getFlags();
        auto auditsFlags = audits.getFlags();
        auto settingsFlags = settings.getFlags();
        auto rolesFlags = roles.getFlags();
        auto keysFlags = keys.getFlags();

        std::vector<std::string> flags;
        flags.reserve(
            identitiesFlags.size() +
            vaultsFlags.size() +
            auditsFlags.size() +
            settingsFlags.size() +
            rolesFlags.size() +
            keysFlags.size()
        );

        auto append = [&](auto &src) { std::move(src.begin(), src.end(), std::back_inserter(flags)); };

        append(identitiesFlags);
        append(vaultsFlags);
        append(auditsFlags);
        append(settingsFlags);
        append(rolesFlags);
        append(keysFlags);

        return flags;
    }

    std::vector<permission::Permission> Admin::toPermissions() const {
        auto identitiesPerms = identities.exportPermissions();
        auto vaultsPerms = vaults.exportPermissions();
        auto auditsPerms = audits.exportPermissions();
        auto settingsPerms = settings.exportPermissions();
        auto rolesPerms = roles.exportPermissions();
        auto keysPerms = keys.exportPermissions();

        std::vector<permission::Permission> perms;
        perms.reserve(
            identitiesPerms.size() +
            vaultsPerms.size() +
            auditsPerms.size() +
            settingsPerms.size() +
            rolesPerms.size() +
            keysPerms.size()
        );

        auto appendMoved = [&](auto &exportResult) {
            auto &src = exportResult();
            std::move(src.begin(), src.end(), std::back_inserter(perms));
        };

        appendMoved(identitiesPerms);
        appendMoved(vaultsPerms);
        appendMoved(auditsPerms);
        appendMoved(settingsPerms);
        appendMoved(rolesPerms);
        appendMoved(keysPerms);

        return perms;
    }

    std::string Admin::toFlagsString() const {
        return identities.toFlagsString() + " " + vaults.toFlagsString() + " " + audits.toFlagsString() + " " + settings
               .toFlagsString() + " " +
               roles.toFlagsString() + " " + keys.toFlagsString();
    }

    Admin Admin::fromJson(const nlohmann::json &j) { return Admin(j); }

    std::vector<Admin> admin_roles_from_pq_res(const pqxx::result &res) {
        std::vector<Admin> roles;
        roles.reserve(res.size());
        for (const auto &row: res) roles.emplace_back(row);
        return roles;
    }

    void to_json(nlohmann::json &j, const Admin &a) {
        j = static_cast<const Meta &>(a);
        j["user_id"] = a.user_id;
        j["permissions"] = a.toPermissions();
    }

    void from_json(const nlohmann::json &j, Admin &a) { a = Admin(j); }

    void to_json(nlohmann::json &j, const std::vector<Admin> &roles) {
        j = nlohmann::json::array();
        for (const auto &role: roles) j.emplace_back(role);
    }

    void to_json(nlohmann::json &j, const std::vector<std::shared_ptr<Admin> > &roles) {
        j = nlohmann::json::array();
        for (const auto &role: roles) j.emplace_back(*role);
    }

    std::string Admin::toString(const uint8_t indent) const {
        std::ostringstream oss;
        oss << std::string(indent, ' ') << snake_case_to_title(name) << " (ID: " << id << ")\n";
        const auto i = indent + 2;
        const std::string in(i, ' ');
        if (user_id) oss << in << "- User ID: " << std::to_string(*user_id) << std::endl;
        oss << Meta::toString(indent)
                << identities.toString(i)
                << vaults.toString(i)
                << audits.toString(i)
                << settings.toString(i)
                << roles.toString(i)
                << keys.toString(i);
        return oss.str();
    }

    std::string to_string(const Admin &r) { return r.toString(); }

    std::string to_string(const std::vector<std::shared_ptr<Admin>> &roles) {
        std::ostringstream oss;
        for (const auto &role: roles) oss << role->toString() << "\n";
        return oss.str();
    }
}
