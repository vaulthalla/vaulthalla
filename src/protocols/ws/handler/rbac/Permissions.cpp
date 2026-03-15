#include "../../../../../include/protocols/ws/handler/rbac/Permissions.hpp"
#include "protocols/ws/Session.hpp"
#include "db/query/rbac/Permission.hpp"
#include "identities/User.hpp"
#include "rbac/permission/Permission.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;

// TODO: Figure out how I want to authenticate this

json Permissions::get(const json& payload, const std::shared_ptr<Session>& session) {
    const auto permissionId = payload.at("id").get<unsigned int>();
    auto permission = db::query::rbac::Permission::getPermission(permissionId);
    if (!permission) throw std::runtime_error("Permission not found");

    return {{"permission", *permission}};
}

json Permissions::getByName(const json& payload, const std::shared_ptr<Session>& session) {
    const auto permissionName = payload.at("name").get<std::string>();
    auto permission = db::query::rbac::Permission::getPermissionByName(permissionName);
    if (!permission) throw std::runtime_error("Permission not found");

    return {{"permission", *permission}};
}

json Permissions::list(const std::shared_ptr<Session>& session) {
    auto permissions = db::query::rbac::Permission::listPermissions();

    return {{"permissions", permissions}};
}
