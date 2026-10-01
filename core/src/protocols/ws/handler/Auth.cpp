#include "protocols/ws/handler/Auth.hpp"
#include "runtime/Deps.hpp"
#include "auth/Manager.hpp"
#include "db/query/rbac/role/Admin.hpp"
#include "auth/session/Validator.hpp"
#include "identities/User.hpp"
#include "db/query/identities/User.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/ShareRateLimit.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "auth/registration/Validator.hpp"

using namespace vh::protocols::ws::handler;
using namespace vh::auth;
using namespace vh::identities;
using namespace vh::rbac::permission::admin;

namespace {
Identities::Type identityTypeFor(const std::shared_ptr<User>& user) {
    if (!user) throw std::runtime_error("User not found");
    return user->isAdmin() ? Identities::Type::Admins : Identities::Type::Users;
}

// Account-management handlers are routed only for authenticated human sessions, but never trust that here: a null
// session user would be a daemon-killing null dereference, not a catchable error.
const std::shared_ptr<User>& requireSessionUser(const std::shared_ptr<vh::protocols::ws::Session>& session) {
    if (!session || !session->user) throw std::runtime_error("User not authenticated");
    return session->user;
}

std::string resetPermissionError(const Identities::Type type) {
    if (type == Identities::Type::Admins)
        return "Permission denied: password reset requires admin identity reset-password permission";
    return "Permission denied: password reset requires user identity reset-password permission";
}
}

json Auth::login(const json &payload, const std::shared_ptr<Session> &session) {
    const auto username = payload.at("name").get<std::string>();
    const auto password = payload.at("password").get<std::string>();

    try {
        runtime::Deps::get().authManager->loginUser(username, password, session);
    } catch (...) {
        // Only failed attempts count toward the auth.login rate limit (#103).
        if (session) vh::protocols::ws::ShareRateLimit::instance().recordLoginFailure(username, *session);
        throw;
    }
    if (!session::Validator::softValidateActiveSession(session)) throw std::runtime_error(
        "Failed to validate session after login");

    session->sendAccessTokenOnNextResponse();
    return {{"user", *session->user}};
}

json Auth::registerUser(const json &payload, const std::shared_ptr<Session> &session) {
    requireSessionUser(session);
    const auto name = payload.at("name").get<std::string>();
    const auto email = payload.at("email").get<std::string>();
    const auto password = payload.at("password").get<std::string>();
    const auto isActive = payload.at("is_active").get<bool>();
    const auto role = payload.at("role").get<std::string>();

    const auto user = std::make_shared<User>(name, email, isActive);

    if (const auto userRole = db::query::rbac::role::Admin::get(role)) {
        if (userRole->name == "super_admin") throw std::runtime_error("Cannot assign super admin role to a user");

        if (userRole->name == "admin" && !session->user->identities().canAdd(Identities::Type::Admins))
            throw std::runtime_error("Permission denied: Only super admins can assign admin role");

        if (!session->user->identities().canAdd(Identities::Type::Users))
            throw std::runtime_error("Permission denied: Only admins can assign user roles");

        user->roles.admin = userRole;
    } else throw std::runtime_error("Invalid role specified: " + role);

    runtime::Deps::get().authManager->registerUser(user, password);

    return {{"user", *user}};
}

json Auth::refreshToken(const std::string &token, const std::shared_ptr<Session> &session) {
    runtime::Deps::get().sessionManager->renewAccessToken(session, token);
    if (!session->user) throw std::runtime_error("Failed to resolve user during token refresh");
    return {{"user", *session->user}};
}

json Auth::deleteUser(const json &payload, const std::shared_ptr<Session> &session) {
    requireSessionUser(session);
    const auto id = payload.at("id").get<unsigned int>();

    const auto targetUser = db::query::identities::User::getUserById(id);
    if (!targetUser) throw std::runtime_error("User not found");

    if (targetUser->isProtected) throw std::runtime_error("Cannot delete protected user");
    if (targetUser->isSuperAdmin()) throw std::runtime_error("Cannot delete super admin user");

    if (targetUser->isAdmin() && !session->user->identities().canDelete(Identities::Type::Admins))
        throw std::runtime_error("Permission denied: Only super admins can delete admin users");

    if (!targetUser->isAdmin() && targetUser->id != session->user->id && !session->user->identities().canDelete(
            Identities::Type::Users))
        throw std::runtime_error("Permission denied: Only admins can delete other users");

    db::query::identities::User::deleteUser(targetUser->id);

    if (db::query::identities::User::getUserById(targetUser->id))
        throw std::runtime_error("Failed to delete user with id: " + std::to_string(targetUser->id));

    runtime::Deps::get().sessionManager->invalidate(std::to_string(targetUser->id));

    return {{"user_id", targetUser->id}};
}

json Auth::updateUser(const json &payload, const std::shared_ptr<Session> &session) {
    if (!session->user) throw std::runtime_error("User not authenticated");

    // The web edit form targets payload.id; the old handler ignored it and mutated the caller instead.
    const auto targetId = payload.contains("id") && !payload.at("id").is_null()
        ? payload.at("id").get<unsigned int>()
        : session->user->id;
    const bool isSelf = targetId == session->user->id;

    const auto target = isSelf ? session->user : db::query::identities::User::getUserById(targetId);
    if (!target) throw std::runtime_error("User not found");
    if (target->isProtected)
        throw std::runtime_error("Protected users cannot be updated through this route");

    if (!isSelf) {
        if (target->isSuperAdmin()) throw std::runtime_error("Cannot update super admin user: " + target->name);
        if (target->isAdmin() && !session->user->admins().canEdit())
            throw std::runtime_error("Permission denied: updating admin users requires admin edit permission");
        if (!target->isAdmin() && !session->user->users().canEdit())
            throw std::runtime_error("Permission denied: updating users requires user edit permission");
    }

    // CLI identity is bound by Linux UID; rebinding it is an operator action on the local CLI only.
    if (payload.contains("linux_uid"))
        throw std::runtime_error("linux_uid can only be changed by an administrator through the local CLI");
    if (payload.contains("updated_by") || payload.contains("protected") || payload.contains("is_protected") ||
        payload.contains("system_only"))
        throw std::runtime_error("Unsupported field in user update");

    if (payload.contains("password") && payload.at("password").is_string() &&
        !payload.at("password").get<std::string>().empty())
        throw std::runtime_error("Use auth.user.change_password to change or reset a password");

    json changes = json::object();
    if (payload.contains("name") && !payload.at("name").is_null()) {
        const auto newName = payload.at("name").get<std::string>();
        if (newName != target->name) {
            if (!vh::auth::registration::Validator::isValidName(newName)) throw std::runtime_error("Invalid user name: " + newName);
            changes["name"] = newName;
        }
    }
    if (payload.contains("email")) changes["email"] = payload.at("email");

    if (payload.contains("is_active") && !payload.at("is_active").is_null()) {
        const auto active = payload.at("is_active").get<bool>();
        if (isSelf && !active) throw std::runtime_error("Cannot deactivate your own account");
        changes["is_active"] = active;
    }

    std::shared_ptr<vh::rbac::role::Admin> newRole;
    if (payload.contains("role") && payload.at("role").is_string()) {
        const auto roleName = payload.at("role").get<std::string>();
        const auto currentRole = target->roles.admin ? target->roles.admin->name : std::string{};
        if (!roleName.empty() && roleName != currentRole) {
            // Role changes are privilege changes: never on your own account, judged by the resolved role.
            if (isSelf) throw std::runtime_error("Cannot change your own role. Ask another administrator to change it.");
            newRole = db::query::rbac::role::Admin::get(roleName);
            if (!newRole) throw std::runtime_error("Invalid role specified: " + roleName);
            if (newRole->name == "super_admin") throw std::runtime_error("Cannot assign super admin role to a user");
            const auto staged = std::make_shared<User>();
            staged->roles.admin = newRole;
            if (staged->isAdmin() && !session->user->admins().canEdit())
                throw std::runtime_error("Permission denied: assigning admin roles requires admin edit permission");
        }
    }

    target->updateUser(changes);
    if (newRole) {
        newRole->user_id = target->id;
        target->roles.admin = newRole;
    }
    target->meta.updated_by = session->user->id;
    runtime::Deps::get().authManager->updateUser(target);

    const auto persisted = db::query::identities::User::getUserById(target->id);
    if (!persisted) throw std::runtime_error("Failed to reload user after update");
    if (isSelf) session->user = persisted;

    return {{"user", *persisted}};
}

json Auth::changePassword(const json &payload, const std::shared_ptr<Session> &session) {
    if (!session || !session->user) throw std::runtime_error("User not authenticated");

    const auto userId = payload.at("id").get<unsigned int>();
    const auto newPassword = payload.at("new_password").get<std::string>();

    std::shared_ptr<User> updatedUser;
    if (userId == session->user->id) {
        if (!payload.contains("old_password") || payload.at("old_password").is_null())
            throw std::runtime_error("Old password is required");

        const auto oldPassword = payload.at("old_password").get<std::string>();
        if (oldPassword.empty()) throw std::runtime_error("Old password is required");

        updatedUser = runtime::Deps::get().authManager->changePassword(userId, oldPassword, newPassword);
        session->user = updatedUser;
    } else {
        const auto targetUser = runtime::Deps::get().authManager->getUser(userId);
        const auto identityType = identityTypeFor(targetUser);
        if (!session->user->identities().canResetPassword(identityType))
            throw std::runtime_error(resetPermissionError(identityType));

        updatedUser = runtime::Deps::get().authManager->resetPassword(userId, newPassword);
    }

    return {{"user", *updatedUser}};
}

json Auth::getUser(const json &payload, const std::shared_ptr<Session> &session) {
    requireSessionUser(session);
    const auto userId = payload.at("id").get<unsigned int>();

    const auto targetUser = db::query::identities::User::getUserById(userId);
    if (!targetUser) throw std::runtime_error("User not found");

    if (session->user->id != targetUser->id) {
        if (targetUser->isAdmin() && !session->user->identities().canView(Identities::Type::Admins))
            throw std::runtime_error("Permission denied: Only super admins can view admin users");

        if (!targetUser->isAdmin() && !session->user->identities().canView(Identities::Type::Users))
            throw std::runtime_error("Permission denied: Only admins can view other users");
    }

    return {{"user", *targetUser}};
}

json Auth::logout(const std::shared_ptr<Session> &session) {
    runtime::Deps::get().sessionManager->invalidate(session);
    return {};
}

json Auth::listUsers(const std::shared_ptr<Session> &session) {
    requireSessionUser(session);
    if (!session->user->identities().canView(Identities::Type::Admins))
        throw std::runtime_error("Permission denied");

    return {{"users", to_json(db::query::identities::User::listUsers())}};
}

json Auth::isUserAuthenticated(const std::string &token, const std::shared_ptr<Session> &session) {
    const bool isAuthenticated = runtime::Deps::get().sessionManager->validate(session, token);
    json data = {{"isAuthenticated", isAuthenticated}};
    if (isAuthenticated) data["user"] = *session->user;
    return data;
}

json Auth::getUserByName(const json &payload, const std::shared_ptr<Session> &session) {
    if (!session->user) throw std::runtime_error("User not authenticated");
    const auto name = payload.at("name").get<std::string>();

    const auto targetUser = db::query::identities::User::getUserByName(name);
    if (!targetUser) throw std::runtime_error("User not found");

    if (session->user->id != targetUser->id) {
        if (targetUser->isAdmin() && !session->user->identities().canView(Identities::Type::Admins))
            throw std::runtime_error("Permission denied: Only super admins can view admin users");

        if (!targetUser->isAdmin() && !session->user->identities().canView(Identities::Type::Users))
            throw std::runtime_error("Permission denied: Only admins can view other users");
    }

    return {{"user", *targetUser}};
}

json Auth::doesAdminHaveDefaultPassword() {
    return {{"isDefault", db::query::identities::User::adminPasswordIsDefault()}};
}
