#include "protocols/ws/handler/rbac/roles/Vault.hpp"
#include "protocols/ws/Session.hpp"
#include "ops/Roles.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/Override.hpp"
#include "ops/Error.hpp"

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

    // "allow" / "deny" (the stored effect names); anything else is refused rather than guessed.
    std::optional<bool> overridePayloadAllow(const json& payload) {
        if (!payload.contains("effect") || payload.at("effect").is_null()) return std::nullopt;
        const auto effect = payload.at("effect").get<std::string>();
        if (effect == "allow") return true;
        if (effect == "deny") return false;
        throw ops::Invalid("override effect must be 'allow' or 'deny', got '" + effect + "'");
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

    // Per-assignment, path-scoped permission overrides. Same ops (and RBAC) as `vh vault role override ...`.

    json Vault::listOverrides(const json& payload, const std::shared_ptr<Session>& session) {
        return {{"overrides", ops::roles::listVaultRoleOverrides(session->user, vaultRolePayloadTarget(payload))}};
    }

    json Vault::addOverrides(const json& payload, const std::shared_ptr<Session>& session) {
        // permissions: [{qualified, value}], value true = allow, false = deny (the role payload shape).
        auto permissions = vaultRolePayloadPermissions(payload);
        permissions.complete = false;
        const auto created = ops::roles::addVaultRoleOverrides(session->user, {
            .target = vaultRolePayloadTarget(payload),
            .permissions = std::move(permissions),
            .pattern = payload.at("pattern").get<std::string>(),
            .enabled = payload.value("enabled", true)
        });
        return {{"overrides", created}};
    }

    json Vault::updateOverride(const json& payload, const std::shared_ptr<Session>& session) {
        const auto updated = ops::roles::updateVaultRoleOverride(session->user, {
            .target = vaultRolePayloadTarget(payload),
            .override_id = payload.at("override_id").get<uint32_t>(),
            .allow = overridePayloadAllow(payload),
            .pattern = vaultRolePayloadString(payload, "pattern"),
            .enabled = payload.contains("enabled") && !payload.at("enabled").is_null()
                           ? std::optional<bool>(payload.at("enabled").get<bool>())
                           : std::nullopt
        });
        return {{"override", updated}};
    }

    json Vault::removeOverride(const json& payload, const std::shared_ptr<Session>& session) {
        ops::roles::removeVaultRoleOverride(session->user, vaultRolePayloadTarget(payload),
                                            payload.at("override_id").get<uint32_t>());
        return {{"removed", true}};
    }

}
