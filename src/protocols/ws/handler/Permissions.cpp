#include "protocols/ws/handler/Permissions.hpp"
#include "protocols/ws/Session.hpp"
#include "db/query/rbac/Permission.hpp"
#include "identities/model/User.hpp"
#include "rbac/model/Permission.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;

json Permissions::get(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can get permissions");

    const auto permissionId = payload.at("id").get<unsigned int>();
    auto permission = db::query::rbac::Permission::getPermission(permissionId);
    if (!permission) throw std::runtime_error("Permission not found");

    return {{"permission", *permission}};
}

json Permissions::getByName(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can get permissions by name");

    const auto permissionName = payload.at("name").get<std::string>();
    auto permission = db::query::rbac::Permission::getPermissionByName(permissionName);
    if (!permission) throw std::runtime_error("Permission not found");

    return {{"permission", *permission}};
}

json Permissions::list(const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can list permissions");

    auto permissions = db::query::rbac::Permission::listPermissions();

    return {{"permissions", permissions}};
}
