#include "auth/Manager.hpp"
#include "identities/model/User.hpp"
#include "auth/session/Manager.hpp"
#include "auth/registration/Validator.hpp"
#include "crypto/util/hash.hpp"
#include "crypto/password/Strength.hpp"
#include "db/query/identities/User.hpp"
#include "storage/Manager.hpp"
#include "protocols/ws/Session.hpp"
#include "log/Registry.hpp"
#include "crypto/secrets/Manager.hpp"
#include "runtime/Deps.hpp"
#include "auth/session/Issuer.hpp"

#include <sodium.h>
#include <stdexcept>
#include <uuid/uuid.h>

using namespace vh::auth;
using namespace vh::auth::model;
using namespace vh::identities::model;
using namespace vh::crypto;
using namespace vh::storage;
using namespace vh::protocols::ws;

namespace vh::auth {

void Manager::registerUser(std::shared_ptr<User> user, const std::string& password) {
    if (!user) throw std::runtime_error("Failed to create user: " + user->name);

    registration::Validator::validateRegistration(user, password);

    user->setPasswordHash(hash::password(password));
    db::query::identities::User::createUser(user);

    user = findUser(user->name);
    runtime::Deps::get().storageManager->initUserStorage(user);

    log::Registry::auth()->info("[AuthManager] User registered: {}", user->name);
}

void Manager::loginUser(const std::string& name, const std::string& password, const std::shared_ptr<Session>& session) {
    auto user = findUser(name);
    if (!user) throw std::runtime_error("User not found: " + name);

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
    users_[user->name] = db::query::identities::User::getUserById(user->id);

    log::Registry::auth()->debug("[AuthManager] User updated: {}", user->name);
}

void Manager::changePassword(const std::string& name, const std::string& oldPassword,
                             const std::string& newPassword) {
    const auto user = findUser(name);
    if (!user) throw std::runtime_error("User not found: " + name);

    if (!hash::verifyPassword(oldPassword, user->password_hash)) throw std::runtime_error(
        "Invalid old password for user: " + name);

    user->setPasswordHash(hash::password(newPassword));

    log::Registry::audit()->info("[AuthManager] User {} is changing password", user->name);
    log::Registry::auth()->info("[AuthManager] Changing password for user: {}", user->name);
}

std::shared_ptr<User> Manager::findUser(const std::string& name) {
    if (users_.contains(name)) return users_[name];

    if (const auto user = db::query::identities::User::getUserByName(name)) {
        users_[name] = user;
        return user;
    }

    throw std::runtime_error("User not found: " + name);
}

}
