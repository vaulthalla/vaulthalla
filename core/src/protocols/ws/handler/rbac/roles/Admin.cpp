#include "protocols/ws/handler/rbac/roles/Admin.hpp"
#include "protocols/ws/Session.hpp"
#include "ops/Roles.hpp"
#include "rbac/role/Admin.hpp"

using namespace vh::rbac;

namespace vh::protocols::ws::handler::rbac::roles {

    namespace {
    // The web's role forms send every permission as {qualified, value}: a complete snapshot.
    ops::roles::PermissionEdit adminRolePayloadPermissions(const json& payload) {
        ops::roles::PermissionEdit edit;
        if (!payload.contains("permissions") || !payload.at("permissions").is_array()) return edit;
        for (const auto& p : payload.at("permissions"))
            edit.changes.emplace_back(p.at("qualified").get<std::string>(), p.at("value").get<bool>());
        edit.complete = true;
        return edit;
    }

    std::optional<std::string> adminRolePayloadString(const json& payload, const char* key) {
        if (!payload.contains(key) || payload.at(key).is_null()) return std::nullopt;
        return payload.at(key).get<std::string>();
    }
    }

    json Admin::add(const json& payload, const std::shared_ptr<Session>& session) {
        const auto created = ops::roles::createAdminRole(session->user, {
            .name = payload.at("name").get<std::string>(),
            .description = payload.value("description", ""),
            .permissions = adminRolePayloadPermissions(payload)
        }, "websocket");
        return {{"role", *created}};
    }

    json Admin::remove(const json& payload, const std::shared_ptr<Session>& session) {
        const auto removed = ops::roles::removeAdminRole(session->user, payload.at("id").get<uint32_t>(), "websocket");
        return {{"role", removed->id}};
    }

    json Admin::update(const json& payload, const std::shared_ptr<Session>& session) {
        const auto updated = ops::roles::updateAdminRole(session->user, {
            .role = payload.at("id").get<uint32_t>(),
            .name = adminRolePayloadString(payload, "name"),
            .description = adminRolePayloadString(payload, "description"),
            .permissions = adminRolePayloadPermissions(payload)
        }, "websocket");
        return {{"role", *updated}};
    }

    json Admin::get(const json& payload, const std::shared_ptr<Session>& session) {
        return {{"role", *ops::roles::getAdminRole(session->user, payload.at("id").get<uint32_t>())}};
    }

    json Admin::getByName(const json& payload, const std::shared_ptr<Session>& session) {
        return {{"role", *ops::roles::getAdminRole(session->user, payload.at("name").get<std::string>())}};
    }

    json Admin::list(const std::shared_ptr<Session>& session) {
        return {{"roles", ops::roles::listAdminRoles(session->user)}};
    }

}
