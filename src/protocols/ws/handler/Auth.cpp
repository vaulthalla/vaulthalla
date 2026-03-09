#include "protocols/ws/handler/Auth.hpp"
#include "runtime/Deps.hpp"
#include "auth/Manager.hpp"
#include "db/query/rbac/Permission.hpp"
#include "auth/session/Validator.hpp"
#include "identities/model/User.hpp"
#include "db/query/identities/User.hpp"
#include "protocols/ws/Session.hpp"
#include "rbac/model/UserRole.hpp"

using namespace vh::protocols::ws::handler;
using namespace vh::auth;
using namespace vh::identities::model;

json Auth::login(const json& payload, const std::shared_ptr<Session>& session) {
    const auto username = payload.at("name").get<std::string>();
    const auto password = payload.at("password").get<std::string>();

    runtime::Deps::get().authManager->loginUser(username, password, session);
    if (!session::Validator::softValidateActiveSession(session)) throw std::runtime_error("Failed to validate session after login");

    session->sendAccessTokenOnNextResponse();
    return {{"user", *session->user}};
}

json Auth::registerUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto name = payload.at("name").get<std::string>();
    const auto email = payload.at("email").get<std::string>();
    const auto password = payload.at("password").get<std::string>();
    const auto isActive = payload.at("is_active").get<bool>();
    const auto role = payload.at("role").get<std::string>();

    const auto user = std::make_shared<User>(name, email, isActive);

    if (const auto userRole = std::dynamic_pointer_cast<rbac::model::UserRole>(db::query::rbac::Permission::getRoleByName(role))) {
        if (!session->user) throw std::runtime_error("User not authenticated");
        if (userRole->name == "super_admin") throw std::runtime_error("Cannot assign super admin role to a user");
        if (userRole->name == "admin" && !session->user->canManageAdmins()) throw std::runtime_error("Permission denied: Only super admins can assign admin role");
        if (!session->user->canManageUsers()) throw std::runtime_error("Permission denied: Only admins can assign user roles");
        user->role = userRole;
    } else throw std::runtime_error("Invalid role specified: " + role);

    runtime::Deps::get().authManager->registerUser(user, password);

    return {{"user", *user}};
}

json Auth::refreshToken(const std::string& token, const std::shared_ptr<Session>& session) {
    runtime::Deps::get().sessionManager->renewAccessToken(session, token);
    session->sendAccessTokenOnNextResponse();
    return {};
}

json Auth::deleteUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto id = payload.at("id").get<unsigned int>();

    const auto targetUser = db::query::identities::User::getUserById(id);
    if (!targetUser) throw std::runtime_error("User not found");

    if (targetUser->isSuperAdmin()) throw std::runtime_error("Cannot delete super admin user");
    if (targetUser->isAdmin() && !session->user->canManageAdmins()) throw std::runtime_error("Permission denied: Only super admins can delete admin users");
    if (!targetUser->isAdmin() && targetUser->id != session->user->id && !session->user->canManageUsers()) throw std::runtime_error("Permission denied: Only admins can delete other users");

    db::query::identities::User::deleteUser(targetUser->id);
    if (db::query::identities::User::getUserById(targetUser->id))
        throw std::runtime_error("Failed to delete user with id: " + std::to_string(targetUser->id));

    runtime::Deps::get().sessionManager->invalidate(std::to_string(targetUser->id));

    return {{ "user_id", targetUser->id }};
}

json Auth::updateUser(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user) throw std::runtime_error("User not authenticated");

    session->user->updateUser(payload);
    runtime::Deps::get().authManager->updateUser(session->user);

    return {{"user", *session->user}};
}

json Auth::changePassword(const json& payload, const std::shared_ptr<Session>& session) {
    const auto oldPassword = payload.at("old_password").get<std::string>();
    const auto newPassword = payload.at("new_password").get<std::string>();

    runtime::Deps::get().authManager->changePassword(session->user->name, oldPassword, newPassword);

    return {};
}

json Auth::getUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto userId = payload.at("id").get<unsigned int>();

    if (session->user->id != userId && !session->user->canManageRoles()) throw std::runtime_error(
        "Permission denied: Only admins can fetch user data");

    const auto requestedUser = db::query::identities::User::getUserById(userId);

    return {{"user", *requestedUser}};
}

json Auth::logout(const std::shared_ptr<Session>& session) {
    runtime::Deps::get().sessionManager->invalidate(session);
    return {};
}

json Auth::listUsers(const std::shared_ptr<Session>& session) {
    if (!session->user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list users");
    return {{"users", to_json(db::query::identities::User::listUsers())}};
}

json Auth::isUserAuthenticated(const std::string& token, const std::shared_ptr<Session>& session)  {
    const bool isAuthenticated = runtime::Deps::get().sessionManager->validate(session, token);
    json data = {{"isAuthenticated", isAuthenticated}};
    if (isAuthenticated) data["user"] = *session->user;
    return data;
}

json Auth::getUserByName(const json& payload, const std::shared_ptr<Session>& session) {
    if (!session->user) throw std::runtime_error("User not authenticated");

    const auto name = payload.at("name").get<std::string>();
    if (session->user->name != name && !session->user->canManageRoles()) throw std::runtime_error(
        "Permission denied: Only admins can fetch user by name");

    const auto retUser = db::query::identities::User::getUserByName(name);
    if (!retUser) throw std::runtime_error("User not found");

    return {{"user", *retUser}};
}

json Auth::doesAdminHaveDefaultPassword() {
    return {{"isDefault", db::query::identities::User::adminPasswordIsDefault()}};
}