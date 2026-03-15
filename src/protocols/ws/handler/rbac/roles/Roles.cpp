#include "protocols/ws/handler/rbac/roles/User.hpp"
#include "protocols/ws/Session.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "db/query/rbac/role/Vault.hpp"
#include "identities/User.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"

using namespace vh::rbac;

namespace vh::protocols::ws::handler::rbac::roles {

    json User::add(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can add roles");
        const auto& type = payload.at("type").get<std::string>();

        if (type == "admin") {
            auto role = std::make_shared<role::Admin>(payload);
            role->id = db::query::rbac::role::Admin::upsert(role);
            return {{"role", *role}};
        }

        if (type == "vault") {
            auto role = std::make_shared<role::Vault>(payload);
            role->id = db::query::rbac::role::Vault::upsert(role);
            return {{"role", *role}};
        }

        throw std::runtime_error("Invalid role type");
    }

    json User::remove(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can delete roles");

        const auto roleId = payload.at("id").get<unsigned int>();
        const auto& type = payload.at("type").get<std::string>();

        if (type == "admin") db::query::rbac::role::Admin::remove(roleId);
        else if (type == "vault") db::query::rbac::role::Vault::remove(roleId);
        else throw std::runtime_error("Invalid role type");

        return {{"role", roleId}};
    }

    json User::update(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can update roles");
        const auto& type = payload.at("type").get<std::string>();

        if (type == "admin") {
            auto role = std::make_shared<role::Admin>(payload);
            db::query::rbac::role::Admin::upsert(role);
            return {{"role", *role}};
        }

        if (type == "vault") {
            auto role = std::make_shared<role::Vault>(payload);
            db::query::rbac::role::Vault::upsert(role);
            return {{"role", *role}};
        }

        throw std::runtime_error("Invalid role type");
    }

    json User::get(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can get roles");

        const auto roleId = payload.at("id").get<unsigned int>();
        const auto type = payload.at("type").get<std::string>();

        if (type == "admin") {
            auto role = db::query::rbac::role::Admin::get(roleId);
            if (!role) throw std::runtime_error("Role not found");
            return {{"role", *role}};
        }

        if (type == "vault") {
            auto role = db::query::rbac::role::Vault::get(roleId);
            if (!role) throw std::runtime_error("Role not found");
            return {{"role", *role}};
        }

        throw std::runtime_error("Invalid role type");
    }

    json User::getByName(const json& payload, const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can get roles by name");

        const auto& type = payload.at("type").get<std::string>();

        if (type == "admin") {
            const auto roleName = payload.at("name").get<std::string>();
            auto role = db::query::rbac::role::Admin::get(roleName);
            if (!role) throw std::runtime_error("Role not found");
            return {{"role", *role}};
        }

        if (type == "vault") {
            const auto roleName = payload.at("name").get<std::string>();
            auto role = db::query::rbac::role::Vault::get(roleName);
            if (!role) throw std::runtime_error("Role not found");
            return {{"role", *role}};
        }

        throw std::runtime_error("Invalid role type");
    }

    json User::listAdminRoles(const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list user roles");
        return {{"roles", db::query::rbac::role::Admin::list()}};
    }

    json User::listVaultRoles(const std::shared_ptr<Session>& session) {
        if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list filesystem roles");
        return {{"roles", db::query::rbac::role::Vault::list()}};
    }

}
