#include "protocols/ws/handler/rbac/roles/Vault.hpp"
#include "protocols/ws/Session.hpp"
#include "ops/Roles.hpp"
#include "rbac/role/Vault.hpp"

using namespace vh::rbac;

namespace vh::protocols::ws::handler::rbac::roles {

    namespace {
    ops::roles::PermissionEdit vaultRolePayloadPermissions(const json& payload) {
        ops::roles::PermissionEdit edit;
        if (!payload.contains("permissions") || !payload.at("permissions").is_array()) return edit;
        for (const auto& p : payload.at("permissions"))
            edit.changes.emplace_back(p.at("qualified").get<std::string>(), p.at("value").get<bool>());
        edit.complete = true;
        return edit;
    }

    std::optional<std::string> vaultRolePayloadString(const json& payload, const char* key) {
        if (!payload.contains(key) || payload.at(key).is_null()) return std::nullopt;
        return payload.at(key).get<std::string>();
    }

    ops::roles::VaultSubject vaultRolePayloadTarget(const json& payload) {
        return {.vault_id = payload.at("vault_id").get<uint32_t>(),
                .subject = {.type = payload.at("subject_type").get<std::string>(),
                            .id = payload.at("subject_id").get<uint32_t>()}};
    }
    }

    json Vault::add(const json& payload, const std::shared_ptr<Session>& session) {
        const auto created = ops::roles::createVaultRole(session->user, {
            .name = payload.at("name").get<std::string>(),
            .description = payload.value("description", ""),
            .permissions = vaultRolePayloadPermissions(payload)
        });
        return {{"role", *created}};
    }

    json Vault::remove(const json& payload, const std::shared_ptr<Session>& session) {
        const auto removed = ops::roles::removeVaultRole(session->user, payload.at("id").get<uint32_t>());
        return {{"role_id", removed->id}};
    }

    json Vault::update(const json& payload, const std::shared_ptr<Session>& session) {
        const auto updated = ops::roles::updateVaultRole(session->user, {
            .role = payload.at("id").get<uint32_t>(),
            .name = vaultRolePayloadString(payload, "name"),
            .description = vaultRolePayloadString(payload, "description"),
            .permissions = vaultRolePayloadPermissions(payload)
        });
        return {{"role", *updated}};
    }

    json Vault::get(const json& payload, const std::shared_ptr<Session>& session) {
        return {{"role", *ops::roles::getVaultRole(session->user, payload.at("id").get<uint32_t>())}};
    }

    json Vault::getByName(const json& payload, const std::shared_ptr<Session>& session) {
        return {{"role", *ops::roles::getVaultRole(session->user, payload.at("name").get<std::string>())}};
    }

    json Vault::list(const std::shared_ptr<Session>& session) {
        return {{"roles", ops::roles::listVaultRoles(session->user)}};
    }

    json Vault::listAssigned(const json &payload, const std::shared_ptr<Session> &session) {
        return {{"assigned_roles", ops::roles::listVaultRoleAssignments(session->user, payload.at("id").get<uint32_t>())}};
    }

    json Vault::assign(const json &payload, const std::shared_ptr<Session> &session) {
        const auto assignment = ops::roles::assignVaultRole(session->user, {
            .target = vaultRolePayloadTarget(payload),
            .role = payload.at("id").get<uint32_t>()
        });
        return {{"assignment", *assignment}};
    }

    json Vault::unassign(const json &payload, const std::shared_ptr<Session> &session) {
        (void)ops::roles::unassignVaultRole(session->user, vaultRolePayloadTarget(payload));
        return {{"unassigned", true}};
    }

}
