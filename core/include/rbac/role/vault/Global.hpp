#pragma once

#include "rbac/role/Meta.hpp"
#include "rbac/role/Vault.hpp"
#include "Base.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <regex>
#include <optional>
#include <unordered_map>

namespace vh::rbac::role::vault {
    struct Global final : BasicMeta, Base, std::enable_shared_from_this<Global> {
        enum class Scope { Self, User, Admin };

        uint32_t user_id{};
        Scope scope{Scope::Self};
        std::optional<uint32_t> template_role_id;
        bool enforce_template{false};

        Global() = default;

        explicit Global(const pqxx::row &row);

        explicit Global(const nlohmann::json &j);

        void updateFromJson(const nlohmann::json &j);

        [[nodiscard]] std::vector<std::string> getFlags() const;

        [[nodiscard]] std::vector<permission::Permission> toPermissions() const;

        [[nodiscard]] static std::string usage();

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

        static Global fromJson(const nlohmann::json &j);

        static Global None(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::None());
        }

        static Global BrowseOnly(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::BrowseOnly());
        }

        static Global Reader(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::Reader());
        }

        static Global Contributor(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::Contributor());
        }

        static Global Editor(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::Editor());
        }

        static Global Manager(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::Manager());
        }

        static Global SyncOperator(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::SyncOperator());
        }

        static Global RoleManager(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::RoleManager());
        }

        static Global PowerUser(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::PowerUser());
        }

        static Global Full(const uint32_t userId, const Scope scope) {
            return make(userId, scope, Base::Full());
        }

        static Global FromVaultRole(
            const uint32_t userId,
            const Scope scope,
            const role::Vault &role,
            const std::optional<uint32_t> templateRoleId = std::nullopt,
            const bool enforceTemplate = false
        ) {
            Base base;
            base.roles = role.roles;
            base.sync = role.sync;
            base.fs = role.fs;

            Global g = Custom(userId, scope, std::move(base), templateRoleId, enforceTemplate);
            return g;
        }

        static Global FromTemplate(
            const uint32_t userId,
            const Scope scope,
            const uint32_t templateRoleId,
            const bool enforceTemplate,
            Base base
        ) {
            Global g = make(userId, scope, std::move(base));
            g.template_role_id = templateRoleId;
            g.enforce_template = enforceTemplate;
            return g;
        }

        static Global Custom(
            const uint32_t userId,
            const Scope scope,
            Base base,
            const std::optional<uint32_t> templateRoleId = std::nullopt,
            const bool enforceTemplate = false
        ) {
            Global g = make(userId, scope, std::move(base));
            g.template_role_id = templateRoleId;
            g.enforce_template = enforceTemplate;
            return g;
        }

    private:
        static Global make(const uint32_t userId, const Scope scope, Base base) {
            Global g;
            g.user_id = userId;
            g.scope = scope;

            g.roles = std::move(base.roles);
            g.sync = std::move(base.sync);
            g.fs = std::move(base.fs);

            return g;
        }
    };

    void to_json(nlohmann::json &j, const Global &r);

    void from_json(const nlohmann::json &j, Global &r);

    std::vector<Global> global_vault_roles_from_json(const nlohmann::json &j);

    std::vector<Global> global_vault_roles_from_pq_result(const pqxx::result &res);

    std::string to_string(const Global &role);

    std::string to_string(const Global::Scope &scope);

    Global::Scope global_vault_role_scope_from_string(const std::string &name);
}
