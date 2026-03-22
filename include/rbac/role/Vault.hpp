#pragma once

#include "rbac/role/Meta.hpp"
#include "vault/Base.hpp"
#include "rbac/permission/Permission.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <regex>
#include <memory>
#include <optional>
#include <unordered_map>

namespace vh::rbac::role {
    struct Vault final : Meta, vault::Base {
        struct AssignmentInfo {
            uint32_t subject_id{}, vault_id{};
            std::string subject_type; // 'user' or 'group'

            [[nodiscard]] std::string toString(uint8_t indent) const;
        };

        std::optional<AssignmentInfo> assignment;

        Vault() = default;

        explicit Vault(const pqxx::row &row);

        Vault(const pqxx::row &row, const pqxx::result &overrides);

        explicit Vault(const nlohmann::json &j);

        [[nodiscard]] static std::string usage();

        [[nodiscard]] std::vector<std::string> getFlags() const;

        [[nodiscard]] std::vector<permission::Permission> toPermissions() const;

        [[nodiscard]] std::string toString(uint8_t indent) const override;

        [[nodiscard]] std::string toString() const { return toString(0); }

        [[nodiscard]] permission::vault::fs::Files &files() noexcept { return fs.files; }
        [[nodiscard]] const permission::vault::fs::Files &files() const noexcept { return fs.files; }

        [[nodiscard]] permission::vault::fs::Directories &directories() noexcept { return fs.directories; }
        [[nodiscard]] const permission::vault::fs::Directories &directories() const noexcept { return fs.directories; }

        [[nodiscard]] permission::vault::Roles &rolesPerms() noexcept { return roles; }
        [[nodiscard]] const permission::vault::Roles &rolesPerms() const noexcept { return roles; }

        [[nodiscard]] permission::vault::sync::Config &syncConfig() noexcept { return sync.config; }
        [[nodiscard]] const permission::vault::sync::Config &syncConfig() const noexcept { return sync.config; }

        [[nodiscard]] permission::vault::sync::Action &syncActions() noexcept { return sync.action; }
        [[nodiscard]] const permission::vault::sync::Action &syncActions() const noexcept { return sync.action; }

        static Vault fromJson(const nlohmann::json &j);

        static Vault ImplicitDeny() {
            return make(
                "implicit_deny",
                "Role that denies all vault permissions.",
                vault::Base::None()
            );
        }

        static Vault Guest() {
            return make(
                "guest",
                "Minimal access role for browsing and limited read-only interaction with vault content.",
                vault::Base::BrowseOnly()
            );
        }

        static Vault Reader() {
            return make(
                "reader",
                "Read-only vault role with access to browse and download content.",
                vault::Base::Reader()
            );
        }

        static Vault Contributor() {
            return make(
                "contributor",
                "Vault role for users who can add and update content without broader administrative control.",
                vault::Base::Contributor()
            );
        }

        static Vault Editor() {
            return make(
                "editor",
                "Vault role for users who can fully edit and reorganize content within the vault.",
                vault::Base::Editor()
            );
        }

        static Vault Manager() {
            return make(
                "manager",
                "Vault manager role with content, sync, and limited role-management authority.",
                vault::Base::Manager()
            );
        }

        static Vault PowerUser() {
            return make(
                "power_user",
                "Advanced vault role with broad filesystem control, sync management, and strong collaborative authority.",
                vault::Base::PowerUser()
            );
        }

        static Vault Full() {
            return make(
                "full",
                "Unrestricted vault role with full permissions across filesystem, sync, and role management.",
                vault::Base::Full()
            );
        }

        static Vault RoleManager() {
            return make(
                "role_manager",
                "Specialized vault role focused on managing vault role assignments and access governance.",
                vault::Base::RoleManager()
            );
        }

        static Vault SyncOperator() {
            return make(
                "sync_operator",
                "Specialized vault role focused on synchronization operations and configuration.",
                vault::Base::SyncOperator()
            );
        }

        static Vault Custom(
            std::string name,
            std::string description,
            const vault::Base& base
        ) {
            return make(std::move(name), std::move(description), base);
        }

    private:
        static Vault make(
            std::string name,
            std::string description,
            vault::Base base
        ) {
            Vault r;
            r.name = std::move(name);
            r.description = std::move(description);

            r.roles = std::move(base.roles);
            r.sync = std::move(base.sync);
            r.fs = std::move(base.fs);

            return r;
        }
    };

    void to_json(nlohmann::json &j, const Vault &r);

    void from_json(const nlohmann::json &j, Vault &r);

    void to_json(nlohmann::json &j, const std::vector<Vault> &roles);

    void to_json(nlohmann::json &j, const std::unordered_map<uint32_t, std::shared_ptr<Vault> > &roles);

    void to_json(nlohmann::json &j, const Vault::AssignmentInfo &r);

    void from_json(const nlohmann::json &j, Vault::AssignmentInfo &r);

    void to_json(nlohmann::json &j, const std::vector<std::shared_ptr<Vault> > &roles);

    std::vector<Vault> vault_roles_from_json(const nlohmann::json &j);

    std::unordered_map<uint32_t, std::shared_ptr<Vault> > vault_roles_from_pq_result(
        const pqxx::result &res, const pqxx::result &overrides);

    std::string to_string(const Vault &role);

    std::string to_string(const std::vector<std::shared_ptr<Vault>> &roles);
}
