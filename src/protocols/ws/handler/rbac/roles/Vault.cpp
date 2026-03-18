#include "protocols/ws/handler/rbac/roles/Vault.hpp"
#include "protocols/ws/Session.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "db/query/rbac/role/vault/Assignments.hpp"
#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "rbac/permission/vault/Roles.hpp"

using namespace vh::rbac;

namespace vh::protocols::ws::handler::rbac::roles {

    json Vault::add(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->vaultRolePerms().canAdd())
            throw std::runtime_error("Permission denied: Only admins can add roles");

        auto role = std::make_shared<role::Vault>(payload);
        role->id = db::query::rbac::role::Vault::upsert(role);
        return {{"role", *role}};
    }

    json Vault::remove(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->vaultRolePerms().canDelete())
            throw std::runtime_error("Permission denied: Only admins can remove roles");

        const auto roleId = payload.at("id").get<unsigned int>();
        db::query::rbac::role::Vault::remove(roleId);
        return {{"role_id", roleId}};
    }

    json Vault::update(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->vaultRolePerms().canEdit())
            throw std::runtime_error("Permission denied: Only admins can update roles");

        auto role = std::make_shared<role::Vault>(payload);
        db::query::rbac::role::Vault::upsert(role);
        return {{"role", *role}};
    }

    json Vault::get(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->vaultRolePerms().canView())
            throw std::runtime_error("Permission denied: Only admins can view roles");

        const auto roleId = payload.at("id").get<unsigned int>();
        auto role = db::query::rbac::role::Vault::get(roleId);
        if (!role) throw std::runtime_error("Role not found");
        return {{"role", *role}};
    }

    json Vault::getByName(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->vaultRolePerms().canView())
            throw std::runtime_error("Permission denied: Only admins can view roles");

        const auto roleName = payload.at("name").get<std::string>();
        auto role = db::query::rbac::role::Vault::get(roleName);
        if (!role) throw std::runtime_error("Role not found");
        return {{"role", *role}};
    }

    json Vault::list(const std::shared_ptr<Session>& session) {
        if (!session->user->vaultRolePerms().canView())
            throw std::runtime_error("Permission denied: Only admins can list roles");

        return {{"roles", db::query::rbac::role::Vault::list()}};
    }

    json Vault::assign(const json &payload, const std::shared_ptr<Session> &session) {
        const auto& subjectType = payload.at("subject_type").get<std::string>();
        const auto& subjectId = payload.at("subject_id").get<uint32_t>();
        const auto& roleId = payload.at("id").get<uint32_t>();
        const auto& vaultId = payload.at("vault_id").get<uint32_t>();

        using Permission = permission::vault::RolePermissions;
        if (!resolver::Vault::has<Permission>({
            .user = session->user,
            .permission = Permission::Assign,
            .target_subject_type = subjectType,
            .target_subject_id = subjectId,
            .vault_id = vaultId
        })) throw std::runtime_error("Permission denied");

        db::query::rbac::role::vault::Assignments::assign(vaultId, subjectType, subjectId, roleId);

        if (const auto& role = db::query::rbac::role::vault::Assignments::get(vaultId, subjectType, subjectId))
            return {{"assignment", *role}};

        throw std::runtime_error("Failed to assign role");
    }

    json Vault::unassign(const json &payload, const std::shared_ptr<Session> &session) {
        const auto& subjectType = payload.at("subject_type").get<std::string>();
        const auto& subjectId = payload.at("subject_id").get<uint32_t>();
        const auto& vaultId = payload.at("vault_id").get<uint32_t>();

        using Permission = permission::vault::RolePermissions;
        if (!resolver::Vault::has<Permission>({
            .user = session->user,
            .permission = Permission::Revoke,
            .target_subject_type = subjectType,
            .target_subject_id = subjectId,
            .vault_id = vaultId
        })) throw std::runtime_error("Permission denied");

        db::query::rbac::role::vault::Assignments::unassign(vaultId, subjectType, subjectId);
        return {{"unassigned", true}};
    }

}
