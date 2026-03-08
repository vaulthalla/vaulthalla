#include "protocols/ws/handler/Roles.hpp"
#include "protocols/ws/Session.hpp"
#include "db/query/rbac/Permission.hpp"
#include "identities/model/User.hpp"
#include "rbac/model/Role.hpp"
#include "rbac/model/VaultRole.hpp"

using namespace vh::protocols::ws::handler;
using namespace vh::rbac::model;

json Roles::add(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can add roles");

    const auto role = std::make_shared<Role>(payload);
    role->id = db::query::rbac::Permission::addRole(role);
    return {{"role", *role}};
}

json Roles::remove(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can delete roles");

    const auto roleId = payload.at("id").get<unsigned int>();
    db::query::rbac::Permission::deleteRole(roleId);
    return {{"role", roleId}};
}

json Roles::update(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can update roles");

    auto role = std::make_shared<Role>(payload);
    db::query::rbac::Permission::updateRole(role);
    return {{"role", *role}};
}

json Roles::get(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can get roles");

    const auto roleId = payload.at("id").get<unsigned int>();
    auto role = db::query::rbac::Permission::getRole(roleId);

    if (!role) throw std::runtime_error("Role not found");

    return {{"role", *role}};
}

json Roles::getByName(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can get roles by name");

    const auto roleName = payload.at("name").get<std::string>();
    auto role = db::query::rbac::Permission::getRoleByName(roleName);
    if (!role) throw std::runtime_error("Role not found");
    return {{"role", *role}};
}

json Roles::list(const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list roles");
    return {{"roles", db::query::rbac::Permission::listRoles()}};
}

json Roles::listUserRoles(const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list user roles");
    return {{"roles", db::query::rbac::Permission::listUserRoles()}};
}

json Roles::listVaultRoles(const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list filesystem roles");
    return {{"roles", db::query::rbac::Permission::listVaultRoles()}};
}
