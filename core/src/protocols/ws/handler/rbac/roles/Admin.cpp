#include "protocols/ws/handler/rbac/roles/Admin.hpp"
#include "protocols/ws/Session.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"

using namespace vh::rbac;

namespace vh::protocols::ws::handler::rbac::roles {

    json Admin::add(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canAdd())
            throw std::runtime_error("Permission denied: Only admins can add role");

        auto role = std::make_shared<role::Admin>(payload);
        role->id = db::query::rbac::role::Admin::upsert(role);
        return {{"role", *role}};
    }

    json Admin::remove(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canDelete())
            throw std::runtime_error("Permission denied: Only admins can remove role");

        const auto roleId = payload.at("id").get<uint32_t>();
        db::query::rbac::role::Admin::remove(roleId);

        return {{"role", roleId}};
    }

    json Admin::update(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canEdit())
            throw std::runtime_error("Permission denied: Only admins can update role");

        auto role = db::query::rbac::role::Admin::get(payload.at("id").get<uint32_t>());
        role->updateFromJson(payload);
        db::query::rbac::role::Admin::upsert(role);
        return {{"role", *role}};
    }

    json Admin::get(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canView())
            throw std::runtime_error("Permission denied: Only admins can view role");

        const auto roleId = payload.at("id").get<uint32_t>();
        auto role = db::query::rbac::role::Admin::get(roleId);
        if (!role) throw std::runtime_error("Role not found");
        return {{"role", *role}};
    }

    json Admin::getByName(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canView())
            throw std::runtime_error("Permission denied: Only admins can view role");

        const auto roleName = payload.at("name").get<std::string>();
        auto role = db::query::rbac::role::Admin::get(roleName);
        if (!role) throw std::runtime_error("Role not found");
        return {{"role", *role}};
    }

    json Admin::list(const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canView())
            throw std::runtime_error("Permission denied: Only admins can view roles");

        return {{"roles", db::query::rbac::role::Admin::list()}};
    }

}
