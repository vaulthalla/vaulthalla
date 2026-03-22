#include "identities/User.hpp"

#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/role/vault/Global.hpp"
#include "rbac/permission/admin/Vaults.hpp"

#include "db/encoding/timestamp.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "protocols/shell/Table.hpp"
#include "log/Registry.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>
#include <pqxx/result>

#include <stdexcept>
#include <utility>

using namespace vh::protocols::shell;
using namespace vh::db::encoding;
using namespace vh::rbac::role;

namespace {
    template<typename T>
    std::optional<T> try_as_optional(const pqxx::row &row, const char *column) {
        try {
            const auto field = row[column];
            if (field.is_null()) return std::nullopt;
            return field.as<T>();
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::string> try_as_optional_string(const pqxx::row &row, const char *column) {
        try {
            const auto field = row[column];
            if (field.is_null()) return std::nullopt;
            return field.as<std::string>();
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<std::time_t> try_as_optional_timestamp(const pqxx::row &row, const char *column) {
        try {
            const auto field = row[column];
            if (field.is_null()) return std::nullopt;
            return parsePostgresTimestamp(field.as<std::string>());
        } catch (...) {
            return std::nullopt;
        }
    }

    std::time_t try_as_timestamp_or_now(const pqxx::row &row, const char *column) {
        try {
            const auto field = row[column];
            if (field.is_null()) return std::time(nullptr);
            return parsePostgresTimestamp(field.as<std::string>());
        } catch (...) {
            return std::time(nullptr);
        }
    }
} // namespace

namespace vh::identities {
    User::User() = default;

    User::User(std::string name, std::string email, const bool isActive)
        : name(std::move(name)) {
        meta.created_at = std::time(nullptr);
        meta.updated_at = meta.created_at;
        meta.is_active = isActive;

        if (!email.empty()) this->email = std::move(email);
    }

    User::User(const pqxx::row &row)
        : id(try_as_optional<uint32_t>(row, "id").value_or(0)),
          name(try_as_optional_string(row, "name").value_or("")),
          password_hash(try_as_optional_string(row, "password_hash").value_or("")) {
        email = try_as_optional_string(row, "email");

        meta.linux_uid = try_as_optional<uint32_t>(row, "linux_uid");
        meta.created_by = try_as_optional<uint32_t>(row, "created_by");

        // Support either new updated_by or legacy last_modified_by while you transition.
        meta.updated_by = try_as_optional<uint32_t>(row, "updated_by");
        if (!meta.updated_by) meta.updated_by = try_as_optional<uint32_t>(row, "last_modified_by");

        meta.created_at = try_as_timestamp_or_now(row, "created_at");
        meta.updated_at = try_as_timestamp_or_now(row, "updated_at");

        meta.last_login = try_as_optional_timestamp(row, "last_login");
        meta.password_changed_at = try_as_optional_timestamp(row, "password_changed_at");
        meta.deactivated_at = try_as_optional_timestamp(row, "deactivated_at");

        meta.is_active = try_as_optional<bool>(row, "is_active").value_or(true);
    }

    User::User(
        const pqxx::row &user,
        const pqxx::row &adminRole,
        const pqxx::result &globalVaultRoles,
        std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault> > &&vRoles,
        std::vector<std::shared_ptr<Group> > &&groups
    ) : User(user) {
        roles.admin = std::make_shared<Admin>(adminRole, globalVaultRoles);
        roles.vaults = std::move(vRoles);
        this->groups = std::move(groups);
    }

    bool User::operator==(const User &other) const {
        return id == other.id
               && meta.linux_uid == other.meta.linux_uid
               && name == other.name
               && email == other.email;
    }

    bool User::operator!=(const User &other) const {
        return !(*this == other);
    }

    rbac::permission::admin::Identities &User::identities() const { return roles.admin->identities; }
    rbac::permission::admin::identities::Users &User::users() const { return identities().users; }
    rbac::permission::admin::identities::Groups &User::groupPerms() const { return identities().groups; }
    rbac::permission::admin::identities::Admins &User::admins() const { return identities().admins; }
    rbac::permission::admin::roles::Admin &User::adminRolePerms() const { return roles.admin->roles.admin; }
    rbac::permission::admin::roles::Vault &User::vaultRolePerms() const { return roles.admin->roles.vault; }
    rbac::permission::admin::Settings &User::settings() const { return roles.admin->settings; }
    rbac::permission::admin::Audits &User::audits() const { return roles.admin->audits; }
    rbac::permission::admin::VaultGlobals &User::vaultGlobals() const { return roles.admin->vGlobals; }
    rbac::permission::admin::Vaults &User::vaultsPerms() const { return roles.admin->vaults; }
    rbac::permission::admin::keys::APIKeys &User::apiKeysPerms() const { return roles.admin->keys.apiKeys; }

    rbac::permission::admin::keys::EncryptionKey &User::encryptionKeysPerms() const {
        return roles.admin->keys.encryptionKeys;
    }

    std::shared_ptr<rbac::role::Vault> User::getDirectVaultRole(const uint32_t vaultId) const {
        std::scoped_lock lock(mutex_);

        const auto it = roles.vaults.find(vaultId);
        if (it == roles.vaults.end()) {
            log::Registry::auth()->debug(
                "User {} has no direct vault role for vault {}",
                name,
                vaultId
            );
            return nullptr;
        }

        log::Registry::auth()->debug(
            "User {} has direct vault role for vault {}",
            name,
            vaultId
        );

        return it->second;
    }

    bool User::hasDirectVaultRole(const uint32_t vaultId) const {
        std::scoped_lock lock(mutex_);
        return roles.vaults.contains(vaultId);
    }

    void User::setPasswordHash(const std::string &hash) {
        std::scoped_lock lock(mutex_);
        password_hash = hash;
        meta.password_changed_at = std::time(nullptr);
    }

    void User::updateUser(const nlohmann::json &j) {
        std::scoped_lock lock(mutex_);

        if (j.contains("name") && !j.at("name").is_null())
            name = j.at("name").get<std::string>();

        if (j.contains("email")) {
            if (j.at("email").is_null()) email = std::nullopt;
            else {
                const auto value = j.at("email").get<std::string>();
                email = value.empty() ? std::nullopt : std::make_optional(value);
            }
        }

        if (j.contains("is_active") && !j.at("is_active").is_null()) {
            meta.is_active = j.at("is_active").get<bool>();
            if (!meta.is_active && !meta.deactivated_at) meta.deactivated_at = std::time(nullptr);
            if (meta.is_active) meta.deactivated_at = std::nullopt;
        }

        if (j.contains("linux_uid")) {
            if (j.at("linux_uid").is_null()) meta.linux_uid = std::nullopt;
            else meta.linux_uid = j.at("linux_uid").get<uint32_t>();
        }

        if (j.contains("updated_by")) {
            if (j.at("updated_by").is_null()) meta.updated_by = std::nullopt;
            else meta.updated_by = j.at("updated_by").get<uint32_t>();
        }

        meta.updated_at = std::time(nullptr);
    }

    void to_json(nlohmann::json &j, const User &u) {
        j = {
            {"id", u.id},
            {"name", u.name},
            {"email", u.email},
            {"created_at", timestampToString(u.meta.created_at)},
            {"updated_at", timestampToString(u.meta.updated_at)},
            {"is_active", u.meta.is_active}
        };

        j["last_login"] = u.meta.last_login
                              ? nlohmann::json(timestampToString(*u.meta.last_login))
                              : nlohmann::json(nullptr);

        j["password_changed_at"] = u.meta.password_changed_at
                                       ? nlohmann::json(timestampToString(*u.meta.password_changed_at))
                                       : nlohmann::json(nullptr);

        j["deactivated_at"] = u.meta.deactivated_at
                                  ? nlohmann::json(timestampToString(*u.meta.deactivated_at))
                                  : nlohmann::json(nullptr);

        if (u.meta.linux_uid) j["linux_uid"] = *u.meta.linux_uid;
        if (u.meta.created_by) j["created_by"] = *u.meta.created_by;
        if (u.meta.updated_by) j["updated_by"] = *u.meta.updated_by;

        {
            std::scoped_lock lock(u.mutex_);

            if (u.roles.admin) j["admin_role"] = *u.roles.admin;
            else {
                throw std::logic_error(
                    "[User] admin role is null for user id=" + std::to_string(u.id) +
                    " name=" + u.name + ". Did you mean to call to_public_json() instead?"
                );
            }

            if (!u.roles.vaults.empty()) j["vault_roles"] = u.roles.vaults;
            else j["vault_roles"] = nlohmann::json::object();
        }
    }

    void from_json(const nlohmann::json &j, User &u) {
        u.id = j.at("id").get<uint32_t>();
        u.name = j.at("name").get<std::string>();

        if (j.contains("email")) {
            if (j.at("email").is_null()) u.email = std::nullopt;
            else {
                const auto value = j.at("email").get<std::string>();
                u.email = value.empty() ? std::nullopt : std::make_optional(value);
            }
        }

        if (j.contains("linux_uid")) {
            if (j.at("linux_uid").is_null()) u.meta.linux_uid = std::nullopt;
            else u.meta.linux_uid = j.at("linux_uid").get<uint32_t>();
        }

        if (j.contains("created_by")) {
            if (j.at("created_by").is_null()) u.meta.created_by = std::nullopt;
            else u.meta.created_by = j.at("created_by").get<uint32_t>();
        }

        if (j.contains("updated_by")) {
            if (j.at("updated_by").is_null()) u.meta.updated_by = std::nullopt;
            else u.meta.updated_by = j.at("updated_by").get<uint32_t>();
        }

        if (j.contains("is_active") && !j.at("is_active").is_null())
            u.meta.is_active = j.at("is_active").get<bool>();

        if (j.contains("admin_role") && !j.at("admin_role").is_null())
            u.roles.admin = std::make_shared<Admin>(j.at("admin_role").get<Admin>());
    }

    nlohmann::json to_json(const std::vector<std::shared_ptr<User> > &users) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto &user: users)
            if (user) j.push_back(*user);
        return j;
    }

    nlohmann::json to_json(const std::shared_ptr<User> &user) {
        if (!user) return nullptr;
        return *user;
    }

    nlohmann::json to_public_json(const std::shared_ptr<User> &u) {
        if (!u) return nullptr;

        return {
            {"id", u->id},
            {"name", u->name},
            {"email", u->email},
            {"is_active", u->meta.is_active}
        };
    }

    nlohmann::json to_public_json(const std::vector<std::shared_ptr<User> > &users) {
        nlohmann::json j = nlohmann::json::array();
        for (const auto &user: users)
            if (user) j.push_back(to_public_json(user));
        return j;
    }

    bool User::isSuperAdmin() const {
        std::scoped_lock lock(mutex_);
        return roles.admin
               && roles.admin->identities.admins.canDelete()
               && roles.admin->vaults.admin.canRemove()
               && roles.admin->keys.encryptionKeys.canRotate()
               && roles.admin->name == "super_admin";
    }

    bool User::isAdmin() const {
        std::scoped_lock lock(mutex_);
        return roles.admin && roles.admin->identities.admins.canDelete() && roles.admin->vaults.admin.canRemove();
    }

    std::string to_string(const std::shared_ptr<User> &user) {
        if (!user) return "No user found\n";

        std::string out;
        out += "User ID: " + std::to_string(user->id) + "\n";
        out += "User: " + user->name + "\n";
        out += "Email: " + user->email.value_or("N/A") + "\n";

        if (user->meta.linux_uid) out += "Linux UID: " + std::to_string(*user->meta.linux_uid) + "\n";
        else out += "Linux UID: Not set\n";

        if (user->meta.created_by) out += "Created By: " + std::to_string(*user->meta.created_by) + "\n";
        else out += "Created By: N/A\n";

        if (user->meta.updated_by) out += "Updated By: " + std::to_string(*user->meta.updated_by) + "\n";
        else out += "Updated By: N/A\n";

        out += "Created At: " + timestampToString(user->meta.created_at) + "\n";
        out += "Updated At: " + timestampToString(user->meta.updated_at) + "\n";
        out += "Last Login: " + (user->meta.last_login ? timestampToString(*user->meta.last_login) : "Never") + "\n";
        out += "Password Changed At: " + (user->meta.password_changed_at
                                              ? timestampToString(*user->meta.password_changed_at)
                                              : "Unknown") + "\n";
        out += "Deactivated At: " + (user->meta.deactivated_at ? timestampToString(*user->meta.deactivated_at) : "N/A")
                + "\n";
        out += "Active: " + std::string(user->meta.is_active ? "Yes" : "No") + "\n"; {
            std::scoped_lock lock(user->mutex_);

            out += "Admin Role: ";
            out += user->roles.admin->toString();

            out += "Direct Vault Roles:\n";
            out += user->roles.vaults.empty() ? "  None\n" : user->roles.admin->vaults.toString(2);

            out += "Groups: " + std::to_string(user->groups.size()) + "\n";
        }

        return out;
    }

    std::string to_string(const std::vector<std::shared_ptr<User> > &users) {
        if (users.empty()) return "No users found\n";

        Table tbl({
                      {"ID", Align::Right, 4, 6, false, false},
                      {"Name", Align::Left, 4, 32, false, false},
                      {"Email", Align::Left, 4, 32, false, false},
                      {"Admin Role", Align::Left, 4, 20, false, false},
                      {"Created At", Align::Left, 4, 20, false, false},
                      {"Last Login", Align::Left, 4, 20, false, false},
                      {"Active", Align::Left, 4, 8, false, false}
                  }, term_width());

        for (const auto &user: users) {
            if (!user) continue;

            tbl.add_row({
                std::to_string(user->id),
                user->name,
                user->email.value_or("N/A"),
                user->roles.admin ? snake_case_to_title(user->roles.admin->name) : "No Role",
                timestampToString(user->meta.created_at),
                user->meta.last_login ? timestampToString(*user->meta.last_login) : "Never",
                user->meta.is_active ? "Yes" : "No"
            });
        }

        return "Vaulthalla users:\n" + tbl.render();
    }
}
