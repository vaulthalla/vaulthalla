#include "protocols/ws/handler/Auth.hpp"
#include "runtime/Deps.hpp"
#include "auth/Manager.hpp"
#include "auth/model/Client.hpp"
#include "db/query/rbac/Permission.hpp"
#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Validator.hpp"
#include "identities/model/User.hpp"
#include "db/query/identities/User.hpp"

using namespace vh::protocols::ws::handler;
using namespace vh::auth;
using namespace vh::identities::model;

json Auth::login(const json& payload, const std::shared_ptr<Session>& session) {
    const auto username = payload.at("name").get<std::string>();
    const auto password = payload.at("password").get<std::string>();

    runtime::Deps::get().authManager->loginUser(username, password, session);
    if (!session::Validator::validateSession(session)) throw std::runtime_error("Failed to validate session after login");
    return {{"token", session->tokens->accessToken->rawToken}, {"user", *session->user}};
}

json Auth::registerUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto name = payload.at("name").get<std::string>();
    const auto email = payload.at("email").get<std::string>();
    const auto password = payload.at("password").get<std::string>();
    const auto isActive = payload.at("is_active").get<bool>();
    const auto role = payload.at("role").get<std::string>();
    const auto userRole = db::query::rbac::Permission::getRoleByName(role);

    runtime::Deps::get().authManager->registerUser(std::make_shared<User>(name, email, isActive), password, session);
    if (!session->user) throw std::runtime_error("Failed to set authenticated user after registration");

    return {{"token", session->tokens->accessToken->rawToken}, {"user", *session->user}};
}

json Auth::deleteUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto id = payload.at("id").get<unsigned int>();
    const auto user = session->getAuthenticatedUser();

    const auto targetUser = db::query::identities::User::getUserById(id);
    if (!targetUser) throw std::runtime_error("User not found");

    if (targetUser->isSuperAdmin()) throw std::runtime_error("Cannot delete super admin user");
    if (targetUser->isAdmin() && !user->canManageAdmins()) throw std::runtime_error("Permission denied: Only super admins can delete admin users");
    if (!targetUser->isAdmin() && targetUser->id != user->id && !user->canManageUsers()) throw std::runtime_error("Permission denied: Only admins can delete other users");

    db::query::identities::User::deleteUser(targetUser->id);
    if (db::query::identities::User::getUserById(targetUser->id))
        throw std::runtime_error("Failed to delete user with id: " + std::to_string(targetUser->id));

    runtime::Deps::get().authManager->sessionManager->invalidateSession(std::to_string(targetUser->id));

    return {{ "user_id", targetUser->id }};
}

json Auth::updateUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto user = session->getAuthenticatedUser();
    if (!user) throw std::runtime_error("User not authenticated");

    user->updateUser(payload);
    runtime::Deps::get().authManager->updateUser(user);

    return {{"user", *user}};
}

json Auth::changePassword(const json& payload, const std::shared_ptr<Session>& session) {
    const auto user = session->getAuthenticatedUser();
    if (!user) throw std::runtime_error("User not authenticated");

    const auto oldPassword = payload.at("old_password").get<std::string>();
    const auto newPassword = payload.at("new_password").get<std::string>();

    runtime::Deps::get().authManager->changePassword(user->name, oldPassword, newPassword);

    return {};
}

json Auth::getUser(const json& payload, const std::shared_ptr<Session>& session) {
    const auto user = session->getAuthenticatedUser();
    if (!user) throw std::runtime_error("User not authenticated");

    const auto userId = payload.at("id").get<unsigned int>();

    if (user->id != userId && !user->canManageRoles()) throw std::runtime_error(
        "Permission denied: Only admins can fetch user data");

    const auto requestedUser = db::query::identities::User::getUserById(userId);

    return {{"user", *requestedUser}};
}

json Auth::refresh(const std::shared_ptr<Session>& session) {
    const auto client = runtime::Deps::get().authManager->validateRefreshToken(session->getRefreshToken(), session);
    if (!client) throw std::runtime_error("No client attached to current websocket session-> Please reauthenticate.");
    if (!client->user) throw std::runtime_error("No user attached to current websocket session-> Please reauthenticate.");
    client->renewToken();
    return {{"token", client->getRawToken()}, {"user", *client->user}};
}

json Auth::logout(const std::shared_ptr<Session>& session) {
    const auto refreshToken = session->getRefreshToken();
    runtime::Deps::get().authManager->sessionManager()->invalidateSession(session->getUUID());
    session->setAuthenticatedUser(nullptr);
    return {};
}

json Auth::listUsers(const std::shared_ptr<Session>& session) {
    if (const auto user = session->getAuthenticatedUser();
        !user->canManageRoles()) throw std::runtime_error("Permission denied: Only admins can list users");
    const auto users = db::query::identities::User::listUsers();
    return {{"users", to_json(users)}};
}

json Auth::isUserAuthenticated(const std::string& token, const std::shared_ptr<Session>& session)  {
    const auto client = runtime::Deps::get().authManager->sessionManager()->getClientSession(session->getUUID());
    if (!client) throw std::runtime_error("No access token attached to current websocket session-> Please reauthenticate.");

    const auto user = client->user;
    if (!user) throw std::runtime_error("User not found for token: " + token);

    const bool isAuthenticated = client->isAuthenticated() && client->validateToken(token);
    json data = {{"isAuthenticated", isAuthenticated}};
    if (isAuthenticated) data["user"] = *user;
    return data;
}

json Auth::getUserByName(const json& payload, const std::shared_ptr<Session>& session) {
    const auto user = session->getAuthenticatedUser();
    if (!user) throw std::runtime_error("User not authenticated");

    const auto name = payload.at("name").get<std::string>();
    if (user->name != name && !user->canManageRoles()) throw std::runtime_error(
        "Permission denied: Only admins can fetch user by name");

    const auto retUser = db::query::identities::User::getUserByName(name);
    if (!retUser) throw std::runtime_error("User not found");

    return {{"user", *retUser}};
}

json Auth::doesAdminHaveDefaultPassword() {
    return {{"isDefault", db::query::identities::User::adminPasswordIsDefault()}};
}