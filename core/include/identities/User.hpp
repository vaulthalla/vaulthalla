#pragma once

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <ctime>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
    class row;
    class result;
}

namespace vh::rbac {
    namespace role {
        struct Admin;
        struct Vault;
    }

    namespace permission {
        struct Admin;

        namespace admin {
            struct Identities;
            struct Audits;
            struct Settings;
            struct Vaults;
            struct VaultGlobals;

            namespace keys {
                struct APIKeys;
                struct EncryptionKey;
            }

            namespace roles {
                struct Admin;
                struct Vault;
            }

            namespace identities {
                struct Users;
                struct Groups;
                struct Admins;
            }
        }
    }
}

namespace vh::identities {
    struct Group;

    struct User : std::enable_shared_from_this<User> {
        struct RoleAssignments {
            std::shared_ptr<rbac::role::Admin> admin{};
            std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>> vaults{};
        };

        struct Meta {
            std::optional<uint32_t> linux_uid{std::nullopt};
            std::optional<uint32_t> created_by{std::nullopt};
            std::optional<uint32_t> updated_by{std::nullopt};

            std::time_t created_at{};
            std::time_t updated_at{};

            std::optional<std::time_t> last_login{std::nullopt};
            std::optional<std::time_t> password_changed_at{std::nullopt};
            std::optional<std::time_t> deactivated_at{std::nullopt};

            bool is_active{true};
        };

        uint32_t id{};
        std::string name{};
        std::string password_hash{};
        std::optional<std::string> email{std::nullopt};

        RoleAssignments roles{};
        std::vector<std::shared_ptr<Group> > groups{};
        Meta meta{};

        mutable std::mutex mutex_{};

        User();

        explicit User(std::string name, std::string email = "", bool isActive = true);

        explicit User(const pqxx::row &row);

        User(
            const pqxx::row &user,
            const pqxx::row &adminRole,
            const pqxx::result &globalVaultRoles,
            std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault> > &&vRoles,
            std::vector<std::shared_ptr<Group> > &&groups
        );

        bool operator==(const User &other) const;

        bool operator!=(const User &other) const;

        [[nodiscard]] std::shared_ptr<rbac::role::Vault> getDirectVaultRole(uint32_t vaultId) const;

        [[nodiscard]] bool hasDirectVaultRole(uint32_t vaultId) const;

        void updateUser(const nlohmann::json &j);

        void setPasswordHash(const std::string &hash);

        [[nodiscard]] bool isAdmin() const;

        [[nodiscard]] bool isSuperAdmin() const;

        [[nodiscard]] rbac::permission::admin::Identities &identities() const;

        [[nodiscard]] rbac::permission::admin::identities::Users &users() const;

        [[nodiscard]] rbac::permission::admin::identities::Admins &admins() const;

        [[nodiscard]] rbac::permission::admin::identities::Groups &groupPerms() const;

        [[nodiscard]] rbac::permission::admin::roles::Admin &adminRolePerms() const;

        [[nodiscard]] rbac::permission::admin::roles::Vault &vaultRolePerms() const;

        [[nodiscard]] rbac::permission::admin::Settings &settings() const;

        [[nodiscard]] rbac::permission::admin::Audits &audits() const;

        [[nodiscard]] rbac::permission::admin::Vaults &vaultsPerms() const;

        [[nodiscard]] rbac::permission::admin::VaultGlobals &vaultGlobals() const;

        [[nodiscard]] rbac::permission::admin::keys::APIKeys &apiKeysPerms() const;

        [[nodiscard]] rbac::permission::admin::keys::EncryptionKey &encryptionKeysPerms() const;
    };

    void to_json(nlohmann::json &j, const User &u);

    void from_json(const nlohmann::json &j, User &u);

    nlohmann::json to_public_json(const std::shared_ptr<User> &u);

    nlohmann::json to_public_json(const std::vector<std::shared_ptr<User> > &u);

    nlohmann::json to_json(const std::vector<std::shared_ptr<User> > &users);

    nlohmann::json to_json(const std::shared_ptr<User> &user);

    std::string to_string(const std::shared_ptr<User> &user);

    std::string to_string(const std::vector<std::shared_ptr<User> > &users);
}
