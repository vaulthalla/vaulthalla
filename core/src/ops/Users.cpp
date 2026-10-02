#include "ops/Users.hpp"

#include "ops/Roles.hpp"
#include "auth/Manager.hpp"
#include "auth/registration/Validator.hpp"
#include "crypto/util/hash.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "identities/User.hpp"
#include "log/Registry.hpp"
#include "rbac/permission/admin/identities/Base.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"

#include <paths.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>

namespace vh::ops::users {

namespace {

using IdPerm = rbac::permission::admin::identities::IdentityPermissions;
using Entity = rbac::resolver::admin::Entity;
using AdminRolePtr = std::shared_ptr<rbac::role::Admin>;

constexpr const char* SUPER_ADMIN_ROLE = "super_admin";

bool roleIsAdminIdentity(const AdminRolePtr& role) { return role && isAdminIdentity(*role); }

bool can(const Actor& actor, const IdPerm perm, const AdminRolePtr& role) {
    return rbac::resolver::Admin::has<IdPerm>({
        .user = actor,
        .permission = perm,
        .identity = roleIsAdminIdentity(role) ? Entity::Admin : Entity::User
    });
}

const char* kindOf(const AdminRolePtr& role) { return roleIsAdminIdentity(role) ? "admin accounts" : "user accounts"; }

std::shared_ptr<identities::User> requireUser(const uint32_t id) {
    auto user = db::query::identities::User::getUserById(id);
    if (!user) throw NotFound("user not found: " + std::to_string(id));
    return user;
}

AdminRolePtr requireRole(const std::string& ref) {
    if (ref.empty()) throw Invalid("a role is required");
    unsigned int id = 0;
    const auto [end, ec] = std::from_chars(ref.data(), ref.data() + ref.size(), id);
    auto role = ec == std::errc{} && end == ref.data() + ref.size() ? db::query::rbac::role::Admin::get(id)
                                                                   : db::query::rbac::role::Admin::get(ref);
    if (!role) throw NotFound("admin role not found: " + ref);
    return role;
}

// The ceiling, for an account's current role or a role about to be assigned.
void requireWithinActor(const Actor& actor, const AdminRolePtr& role, const std::string& refusal) {
    if (!role || actor->isSuperAdmin()) return;
    const auto beyond = roles::permissionsBeyondActor(actor, *role);
    if (beyond.empty()) return;
    std::string msg = refusal + ":";
    for (const auto& name : beyond) msg += " " + name;
    throw Denied(msg);
}

void requireAssignable(const Actor& actor, const AdminRolePtr& role) {
    if (role->name == SUPER_ADMIN_ROLE) throw Denied("the super_admin role cannot be assigned");
    requireWithinActor(actor, role, "you cannot assign a role with admin permissions you do not hold");
}

// Managing an existing account: its class decides the permission, and an account above the actor is off limits.
void requireManage(const Actor& actor, const std::shared_ptr<identities::User>& target, const IdPerm perm,
                   const std::string& verb) {
    if (target->isProtected) throw Denied("protected accounts cannot be " + verb + " here: " + target->name);
    if (target->isSuperAdmin()) throw Denied("the super admin account cannot be " + verb + ": " + target->name);
    if (!can(actor, perm, target->roles.admin))
        throw Denied(std::string("you do not have permission to manage ") + kindOf(target->roles.admin));
    requireWithinActor(actor, target->roles.admin,
                       "you cannot manage an account whose role grants admin permissions you do not hold");
}

void requireValidName(const std::string& name) {
    if (!auth::registration::Validator::isValidName(name))
        throw Invalid("user names must be 3 to 50 characters: " + name);
}

void requireValidEmail(const std::optional<std::string>& email) {
    if (email && !email->empty() && !auth::registration::Validator::isValidEmail(*email))
        throw Invalid("invalid email address: " + *email);
}

void requireBindableUid(const uint32_t uid, const std::optional<uint32_t> currentOwner) {
    if (uid == 0) throw Invalid("linux uid must be a positive integer");
    if (uid == ::getuid()) throw Invalid("the Vaulthalla service account cannot be bound to a user");
    if (const auto holder = db::query::identities::User::getUserByLinuxUID(uid); holder && holder->id != currentOwner)
        throw Conflict("linux uid " + std::to_string(uid) + " is already bound to " + holder->name);
}

std::string generatePassword() {
    const std::size_t length = paths::testMode ? 8 : 84;
    for (int attempt = 0; attempt < 4096; ++attempt)
        if (auto password = crypto::hash::generate_secure_password(length);
            auth::registration::Validator::isValidPassword(password)) return password;
    throw std::runtime_error("failed to generate a password that meets the password policy");
}

}

bool isAdminIdentity(const rbac::role::Admin& role) {
    return role.identities.toMask() || role.audits.toMask() || role.settings.toMask() || role.roles.toMask() ||
           role.s3Gateway.toMask() || role.vaults.admin.raw() || role.vaults.user.raw() ||
           role.keys.apiKeys.admin.raw() || role.keys.apiKeys.user.raw() || role.keys.encryptionKeys.raw();
}

Created create(const Actor& actor, const Create& req) {
    requireActor(actor);
    const auto role = requireRole(req.role);
    if (!can(actor, IdPerm::Add, role)) throw Denied(std::string("you do not have permission to create ") + kindOf(role));
    requireAssignable(actor, role);

    requireValidName(req.name);
    requireValidEmail(req.email);
    if (db::query::identities::User::getUserByName(req.name)) throw Conflict("a user named '" + req.name + "' already exists");
    if (req.linux_uid) requireBindableUid(*req.linux_uid, std::nullopt);

    auto user = std::make_shared<identities::User>(req.name, req.email.value_or(""), req.is_active);
    if (!req.email || req.email->empty()) user->email = std::nullopt;
    user->roles.admin = role;
    user->meta.linux_uid = req.linux_uid;
    user->meta.created_by = actor->id;
    user->meta.updated_by = actor->id;

    Created out;
    if (!req.password) out.generated_password = generatePassword();
    try {
        runtime::Deps::get().authManager->registerUser(user, req.password ? *req.password : *out.generated_password);
    } catch (const std::exception& e) {
        // The registration validator refuses a weak or breached password; that is the request's fault.
        throw Invalid(e.what());
    }
    out.user = requireUser(user->id);
    log::Registry::audit()->info("[ops::users] {} created user {} with role {}", actor->name, req.name, role->name);
    return out;
}

UserPtr update(const Actor& actor, const Update& req) {
    requireActor(actor);
    const auto target = requireUser(req.id);
    const bool isSelf = target->id == actor->id;
    if (isSelf) {
        if (target->isProtected) throw Denied("protected accounts cannot be updated here: " + target->name);
    } else {
        requireManage(actor, target, IdPerm::Edit, "updated");
    }

    bool endSessions = false;
    if (req.name && *req.name != target->name) {
        if (target->isSuperAdmin()) throw Denied("the super admin account cannot be renamed");
        requireValidName(*req.name);
        if (db::query::identities::User::getUserByName(*req.name))
            throw Conflict("a user named '" + *req.name + "' already exists");
        target->name = *req.name;
    }
    if (req.email) {
        requireValidEmail(*req.email);
        target->email = *req.email && !(*req.email)->empty() ? *req.email : std::nullopt;
    }
    if (req.is_active && *req.is_active != target->meta.is_active) {
        if (isSelf) throw Denied("you cannot deactivate or reactivate your own account");
        target->meta.is_active = *req.is_active;
        if (!*req.is_active) {
            target->meta.deactivated_at = std::time(nullptr);
            endSessions = true;
        } else {
            target->meta.deactivated_at = std::nullopt;
        }
    }
    if (req.role) {
        const auto role = requireRole(*req.role);
        if (!target->roles.admin || role->id != target->roles.admin->id) {
            // Role changes are privilege changes: never on your own account, and judged by the resolved role.
            if (isSelf) throw Denied("you cannot change your own role; ask another administrator");
            requireAssignable(actor, role);
            if (!can(actor, IdPerm::Edit, role))
                throw Denied(std::string("you do not have permission to manage ") + kindOf(role));
            role->user_id = target->id;
            target->roles.admin = role;
            endSessions = true;   // live sessions carry the old role
        }
    }
    if (req.linux_uid && req.linux_uid != target->meta.linux_uid) {
        // The CLI authenticates by Linux UID: rebinding your own would let you take over another account's.
        if (isSelf) throw Denied("you cannot change your own Linux UID binding; ask another administrator");
        requireBindableUid(*req.linux_uid, target->id);
        target->meta.linux_uid = req.linux_uid;
    }

    target->meta.updated_by = actor->id;
    runtime::Deps::get().authManager->updateUser(target);
    if (endSessions) runtime::Deps::get().authManager->revokeSessions(target->id);
    return requireUser(target->id);
}

UserPtr remove(const Actor& actor, const uint32_t id) {
    requireActor(actor);
    const auto target = requireUser(id);
    if (target->id == actor->id) throw Denied("you cannot delete your own account; ask another administrator");
    requireManage(actor, target, IdPerm::Delete, "deleted");

    runtime::Deps::get().authManager->revokeSessions(target->id);
    db::query::identities::User::deleteUser(target->id);
    if (db::query::identities::User::getUserById(target->id))
        throw std::runtime_error("failed to delete user " + std::to_string(target->id));
    log::Registry::audit()->info("[ops::users] {} deleted user {}", actor->name, target->name);
    return target;
}

UserPtr get(const Actor& actor, const uint32_t id) {
    requireActor(actor);
    auto target = requireUser(id);
    if (target->id != actor->id && !can(actor, IdPerm::View, target->roles.admin))
        throw Denied(std::string("you do not have permission to view ") + kindOf(target->roles.admin));
    return target;
}

UserPtr getByName(const Actor& actor, const std::string& name) {
    requireActor(actor);
    const auto target = db::query::identities::User::getUserByName(name);
    if (!target) throw NotFound("user not found: " + name);
    return get(actor, target->id);
}

std::vector<UserPtr> list(const Actor& actor, db::model::ListQueryParams params) {
    requireActor(actor);
    const bool users = can(actor, IdPerm::View, nullptr);
    const bool admins = rbac::resolver::Admin::has<IdPerm>({.user = actor, .permission = IdPerm::View, .identity = Entity::Admin});
    if (!users && !admins) throw Denied("you do not have permission to list users");

    auto all = db::query::identities::User::listUsers(std::move(params));
    if (users && admins) return all;
    std::erase_if(all, [&](const UserPtr& u) {
        return u->id != actor->id && roleIsAdminIdentity(u->roles.admin) != admins;
    });
    return all;
}

UserPtr changePassword(const Actor& actor, const uint32_t id, const std::optional<std::string>& currentPassword,
                       const std::string& newPassword) {
    requireActor(actor);
    const auto target = requireUser(id);
    const auto& auth = runtime::Deps::get().authManager;
    try {
        if (target->id == actor->id) {
            if (!currentPassword || currentPassword->empty()) throw Invalid("your current password is required");
            return auth->changePassword(target->id, *currentPassword, newPassword);
        }
        requireManage(actor, target, IdPerm::ResetPassword, "reset");
        auto updated = auth->resetPassword(target->id, newPassword);
        auth->revokeSessions(target->id);
        return updated;
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        // Wrong current password, or a new one the policy refuses.
        throw Invalid(e.what());
    }
}

}
