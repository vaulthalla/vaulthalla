#include "protocols/ws/handler/rbac/roles/Admin.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/RoleGuards.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "identities/User.hpp"
#include "notifications/SecurityAlertProducer.hpp"
#include "rbac/role/Admin.hpp"

using namespace vh::rbac;

namespace vh::protocols::ws::handler::rbac::roles {

    json Admin::add(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canAdd())
            throw std::runtime_error("Permission denied: Only admins can add role");

        // Create is insert-only: an existing name (including super_admin) is an error, never an overwrite.
        const auto staged = std::make_shared<vh::rbac::role::Admin>(payload);
        const auto created = protocols::roles::createAdminRole(staged);
        notifications::enqueueAdminRoleCreated(created, notifications::actorFromUser("websocket", session->user));
        return {{"role", *created}};
    }

    json Admin::remove(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canDelete())
            throw std::runtime_error("Permission denied: Only admins can remove role");

        const auto roleId = payload.at("id").get<uint32_t>();
        const auto existing = db::query::rbac::role::Admin::get(roleId);
        if (!existing) throw std::runtime_error("Role not found");
        if (const auto denied = protocols::roles::adminRoleDeleteError(*session->user, *existing))
            throw std::runtime_error(*denied);

        db::query::rbac::role::Admin::remove(roleId);
        notifications::enqueueAdminRoleDeleted(existing, notifications::actorFromUser("websocket", session->user));

        return {{"role", roleId}};
    }

    json Admin::update(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->adminRolePerms().canEdit())
            throw std::runtime_error("Permission denied: Only admins can update role");

        const auto existing = db::query::rbac::role::Admin::get(payload.at("id").get<uint32_t>());
        if (!existing) throw std::runtime_error("Role not found");

        const auto updated = std::make_shared<vh::rbac::role::Admin>(*existing);
        updated->updateFromJson(payload);
        if (const auto denied = protocols::roles::adminRoleUpdateError(*session->user, *existing, *updated))
            throw std::runtime_error(*denied);

        db::query::rbac::role::Admin::upsert(updated);
        notifications::enqueueAdminRoleUpdated(updated, notifications::actorFromUser("websocket", session->user));
        return {{"role", *updated}};
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
