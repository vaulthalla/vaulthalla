#include "auth/Manager.hpp"
#include "identities/User.hpp"
#include "auth/session/Manager.hpp"
#include "auth/registration/Validator.hpp"
#include "crypto/util/hash.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/auth/RefreshToken.hpp"
#include "storage/Manager.hpp"
#include "protocols/ws/Session.hpp"
#include "log/Registry.hpp"
#include "crypto/secrets/Manager.hpp"
#include "runtime/Deps.hpp"
#include "auth/session/Issuer.hpp"
#include "auth/Bootstrap.hpp"

#include <sodium.h>
#include <stdexcept>
#include <uuid/uuid.h>

using namespace vh::auth;
using namespace vh::auth::model;
using namespace vh::identities;
using namespace vh::crypto;
using namespace vh::storage;
using namespace vh::protocols::ws;

namespace vh::auth {
namespace {
void validateNewPassword(const std::string& newPassword) {
    if (const auto violation = registration::Validator::passwordPolicyViolation(newPassword))
        throw std::runtime_error("New password does not meet password policy: " + *violation);
}

std::string hashNewPassword(const std::string& newPassword) {
    validateNewPassword(newPassword);

    auto passwordHash = hash::password(newPassword);
    if (passwordHash.empty()) throw std::runtime_error("Failed to hash new password");
    return passwordHash;
}
}

void Manager::registerUser(std::shared_ptr<User> user, const std::string& password) {
    if (!user) throw std::runtime_error("Failed to create user: " + user->name);

    registration::Validator::validateRegistration(user, password);

    user->setPasswordHash(hash::password(password));
    db::query::identities::User::createUser(user);

    user = getUser(user->name);
    try {
        runtime::Deps::get().storageManager->initUserStorage(user);
    } catch (...) {
        // Don't leave a half-registered account behind an error response.
        db::query::identities::User::deleteUser(user->id);
        throw;
    }

    log::Registry::auth()->info("[AuthManager] User registered: {}", user->name);
}

void Manager::loginUser(const std::string& name, const std::string& password, const std::shared_ptr<Session>& session) {
    auto user = getUser(name);
    if (!user) throw std::runtime_error("User not found: " + name);
    if (user->systemOnly) throw std::runtime_error("System-only users cannot log in");
    if (!user->meta.is_active) throw std::runtime_error("This account is deactivated");

    if (!hash::verifyPassword(password, user->password_hash)) throw std::runtime_error(
        "Invalid password for user: " + name);

    db::query::identities::User::updateLastLoggedInUser(user->id);
    user = db::query::identities::User::getUserById(user->id);

    session->setAuthenticatedUser(user);
    runtime::Deps::get().sessionManager->promote(session);

    log::Registry::auth()->info("[AuthManager] User logged in: {}", user->name);
}

void Manager::updateUser(const std::shared_ptr<User>& user) {
    if (!user) throw std::runtime_error("Cannot update null user");

    db::query::identities::User::updateUser(user);

    log::Registry::auth()->debug("[AuthManager] User updated: {}", user->name);
}

std::shared_ptr<User> Manager::changePassword(const uint32_t userId, const std::string& oldPassword,
                                              const std::string& newPassword) {
    const auto user = getUser(userId);
    if (!user) throw std::runtime_error("User not found: " + std::to_string(userId));
    if (user->systemOnly) throw std::runtime_error("System-only users cannot change passwords");

    if (!hash::verifyPassword(oldPassword, user->password_hash)) throw std::runtime_error(
        "Invalid old password for user: " + user->name);

    db::query::identities::User::updateUserPassword(user->id, hashNewPassword(newPassword));
    const auto updatedUser = db::query::identities::User::getUserById(user->id);
    if (!updatedUser) throw std::runtime_error("Failed to reload user after password change: " + user->name);

    log::Registry::audit()->info("[AuthManager] User {} is changing password", user->name);
    log::Registry::auth()->info("[AuthManager] Changing password for user: {}", user->name);
    if (updatedUser->name == bootstrap::kSuperAdminName) (void)bootstrap::onSuperAdminPasswordChanged();

    return updatedUser;
}

std::shared_ptr<User> Manager::resetPassword(const uint32_t userId, const std::string& newPassword) {
    const auto user = getUser(userId);
    if (!user) throw std::runtime_error("User not found: " + std::to_string(userId));
    if (user->systemOnly) throw std::runtime_error("System-only users cannot receive normal user passwords");

    db::query::identities::User::updateUserPassword(user->id, hashNewPassword(newPassword));
    const auto updatedUser = db::query::identities::User::getUserById(user->id);
    if (!updatedUser) throw std::runtime_error("Failed to reload user after password reset: " + user->name);

    log::Registry::audit()->info("[AuthManager] Password reset for user {}", user->name);
    log::Registry::auth()->info("[AuthManager] Reset password for user: {}", user->name);
    if (updatedUser->name == bootstrap::kSuperAdminName) (void)bootstrap::onSuperAdminPasswordChanged();

    return updatedUser;
}

// Always the database: accounts change through the CLI and the web alike, and a login or a password check
// against a cached copy honoured renamed, deactivated, demoted and even deleted accounts.
std::shared_ptr<User> Manager::getUser(const std::string& name) {
    if (const auto user = db::query::identities::User::getUserByName(name)) return user;
    throw std::runtime_error("User not found: " + name);
}

std::shared_ptr<identities::User> Manager::getUser(const uint32_t id) {
    if (const auto user = db::query::identities::User::getUserById(id)) return user;
    throw std::runtime_error("User not found: " + std::to_string(id));
}

void Manager::revokeSessions(const uint32_t userId) {
    // Refresh tokens first, so no live session can rehydrate from one while the runtime sessions are dropped.
    db::query::auth::RefreshToken::revokeAll(userId);
    if (const auto& sessions = runtime::Deps::get().sessionManager)
        for (const auto& session : sessions->getSessionsByUserId(userId)) {
            try {
                sessions->invalidate(session);
            } catch (const std::exception& e) {
                log::Registry::auth()->error("[AuthManager] Failed to invalidate a session of user id {}: {}", userId, e.what());
            }
        }
    log::Registry::auth()->info("[AuthManager] Revoked sessions for user id {}", userId);
}
}
