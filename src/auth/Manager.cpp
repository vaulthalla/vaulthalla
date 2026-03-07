#include "auth/Manager.hpp"
#include "identities/model/User.hpp"
#include "auth/model/Client.hpp"
#include "auth/session/Manager.hpp"
#include "crypto/util/hash.hpp"
#include "crypto/password/Strength.hpp"
#include "db/query/identities/User.hpp"
#include "storage/Manager.hpp"
#include "protocols/ws/Session.hpp"
#include "config/ConfigRegistry.hpp"
#include "log/Registry.hpp"

#include <chrono>
#include <sodium.h>
#include <stdexcept>
#include <uuid/uuid.h>
#include <ranges>
#include <paths.h>

using namespace vh::auth::model;
using namespace vh::identities::model;
using namespace vh::crypto;
using namespace vh::storage;
using namespace vh::protocols;

namespace vh::auth {

Manager::Manager(const std::shared_ptr<storage::Manager>& storageManager)
    : sessionManager(std::make_shared<session::Manager>()), storageManager_(storageManager) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed in AuthManager");
}

void Manager::rehydrateOrCreateClient(const std::shared_ptr<ws::Session>& session) const {
    std::shared_ptr<Session> s;

    if (!session->refreshToken->rawToken.empty()) {
        log::Registry::auth()->debug("[AuthManager] Attempting to rehydrate session from provided refresh token.");
        if (const auto validatedClient = validateRefreshToken(session->refreshToken->rawToken, session); !
            validatedClient) log::Registry::auth()->debug(
            "[AuthManager] Failed to rehydrate session: invalid refresh token.");
        else {
            s = validatedClient;
            log::Registry::auth()->debug("[AuthManager] Successfully rehydrated session with UUID: {}", s->uuid);
        }
    }

    if (!s) session->refreshToken = createRefreshToken(session);
    sessionManager->ensureSession(s);
}

std::shared_ptr<Session> Manager::registerUser(std::shared_ptr<User> user,
                                              const std::string& password,
                                              const std::shared_ptr<ws::Session>& session) {
    isValidRegistration(user, password);

    user->setPasswordHash(hash::password(password));
    db::query::identities::User::createUser(user);

    user = findUser(user->name);
    auto client = sessionManager->getSession(session->uuid);
    session->user = user;

    sessionManager->promoteSession(client);
    storageManager_->initUserStorage(user);

    if (!user) throw std::runtime_error("Failed to create user: " + user->name);

    log::Registry::auth()->info("[AuthManager] User registered: {}", user->name);
    return client;
}

std::shared_ptr<Session> Manager::loginUser(const std::string& name, const std::string& password,
                                           const std::shared_ptr<ws::Session>& session) {
    try {
        auto user = findUser(name);
        if (!user) throw std::runtime_error("User not found: " + name);

        if (!hash::verifyPassword(password, user->password_hash)) throw std::runtime_error(
            "Invalid password for user: " + name);

        db::query::identities::User::revokeAllRefreshTokens(user->id);
        db::query::identities::User::updateLastLoggedInUser(user->id);
        user = db::query::identities::User::getUserById(user->id);
        auto client = sessionManager->getSession(session->uuid);
        session->user = user;

        sessionManager->promoteSession(client);

        log::Registry::auth()->info("[AuthManager] User logged in: {}", user->name);
        return client;
    } catch (const std::exception& e) {
        log::Registry::auth()->error("[AuthManager] loginUser failed: {}", e.what());
        return nullptr;
    }
}

void Manager::updateUser(const std::shared_ptr<User>& user) {
    try {
        if (!user) throw std::runtime_error("Cannot update null user");

        db::query::identities::User::updateUser(user);
        users_[user->name] = db::query::identities::User::getUserById(user->id);

        log::Registry::auth()->debug("[AuthManager] User updated: {}", user->name);
    } catch (const std::exception& e) {
        log::Registry::auth()->error("[AuthManager] updateUser failed: {}", e.what());
        throw std::runtime_error("Failed to update user: " + user->name + " - " + e.what());
    }
}

void Manager::validateRefreshToken(const std::string& refreshToken) const {
    const auto decoded = jwt::decode<jwt::traits::nlohmann_json>(refreshToken);
    const std::string tokenJti = decoded.get_id();

    if (const auto session = sessionManager->getSession(tokenJti)) {
        if (!session::Validator::validateSession(session)) throw std::runtime_error("Prior session is not authenticated");
        return;
    }

    throw std::runtime_error("HTTP validation requires an existing websocket authenticated session. "
                             "Please authenticate via WebSocket first. "
                             "Session not found for token JTI: " + tokenJti);
}

std::shared_ptr<Session> Manager::validateRefreshToken(const std::string& refreshToken,
                                                      const std::shared_ptr<ws::Session>& session) const {
    try {
        // 1. Decode and verify JWT
        const auto decoded = jwt::decode<jwt::traits::nlohmann_json>(refreshToken);
        const std::string tokenJti = decoded.get_id();

        if (const auto priorSession = sessionManager->getSession(tokenJti)) {
            if (!session::Validator::validateSession(priorSession)) throw std::runtime_error("Prior session is not authenticated");
            log::Registry::auth()->debug("[AuthManager] Rehydrating existing session for JTI: {}", tokenJti);
            sessionManager->ensureSession(session);
            return priorSession; // Session already exists, rehydrate it
        }

        const auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
            .allow_algorithm(jwt::algorithm::hs256{jwt_secret_})
            .with_issuer("Vaulthalla");

        verifier.verify(decoded);

        // 2. Extract and validate claims
        const std::string tokenSub = decoded.get_subject();
        const auto tokenExp = decoded.get_expires_at();

        if (tokenExp < std::chrono::system_clock::now()) throw std::runtime_error("Refresh token has expired");

        if (tokenJti.empty()) throw std::runtime_error("Missing JTI in refresh token");

        // 3. Lookup refresh token by jti
        auto storedToken = db::query::identities::User::getRefreshToken(tokenJti);
        if (!storedToken) {
            log::Registry::auth()->debug("[AuthManager] Refresh token not found: {}", tokenJti);
            return nullptr;
        }

        if (storedToken->revoked) throw std::runtime_error("Refresh token has been revoked");

        if (std::chrono::system_clock::from_time_t(storedToken->expiresAt) < std::chrono::system_clock::now()) throw
            std::runtime_error("Refresh token has expired");

        // 4. Secure compare stored hash to hashed input token
        if (!hash::verifyPassword(refreshToken, storedToken->hashedToken)) throw std::runtime_error(
            "Refresh token hash mismatch");

        auto user = db::query::identities::User::getUserByRefreshToken(tokenJti);
        if (!user) {
            log::Registry::auth()->debug("[AuthManager] User not found for refresh token: {}", tokenJti);
            return nullptr;
        }

        sessionManager->ensureSession(session);

        return session;
    } catch (const std::exception& e) {
        // making this debug to avoid flooding logs with errors
        log::Registry::auth()->debug("[AuthManager] validateRefreshToken failed: {}", e.what());
        return nullptr;
    }
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

bool Manager::isValidRegistration(const std::shared_ptr<User>& user, const std::string& password) {
    std::vector<std::string> errors;

    if (!isValidName(user->name)) errors.emplace_back("Name must be between 3 and 50 characters.");

    if (user->email && !isValidEmail(*user->email)) errors.emplace_back("Email must be valid and contain '@' and '.'.");

    if (!paths::testMode) {
        if (const auto strength = password::Strength::passwordStrengthCheck(password) < 50)
            errors.emplace_back("Password is too weak (strength " + std::to_string(strength) +
                                "/100). Use at least 12 characters, mix upper/lowercase, digits, and symbols.");

        if (password::Strength::containsDictionaryWord(password)) errors.emplace_back(
            "Password contains dictionary word — this is forbidden.");

        if (password::Strength::isCommonWeakPassword(password)) errors.emplace_back(
            "Password matches known weak pattern — this is forbidden.");

        if (password::Strength::isPwnedPassword(password)) errors.emplace_back(
            "Password has been found in public breaches — choose a different one.");
    }

    if (!errors.empty()) {
        std::ostringstream oss;
        oss << "Registration failed due to the following issues:\n";
        for (const auto& err : errors) oss << "- " << err << std::endl;
        log::Registry::auth()->error("[AuthManager] Registration validation failed: {}", oss.str());
        throw std::runtime_error(oss.str());
    }

    return true;
}

bool Manager::isValidName(const std::string& displayName) {
    return !displayName.empty() && displayName.size() > 2 && displayName.size() <= 50;
}

bool Manager::isValidEmail(const std::string& email) {
    return !email.empty() && email.find('@') != std::string::npos && email.find('.') != std::string::npos;
}

bool Manager::isValidPassword(const std::string& password) {
    if (paths::testMode) return true;
    std::vector<std::string> errors;
    if (password::Strength::passwordStrengthCheck(password) < 50) return false;
    if (password::Strength::containsDictionaryWord(password)) return false;
    if (password::Strength::isCommonWeakPassword(password)) return false;
    if (password::Strength::isPwnedPassword(password)) return false;
    return !password.empty() && password.size() >= 8 && password.size() <= 128 &&
           std::ranges::any_of(password.begin(), password.end(), ::isdigit) && // At least one digit
           std::ranges::any_of(password.begin(), password.end(), ::isalpha);   // At least one letter
}

bool Manager::isValidGroup(const std::string& group) {
    return !group.empty() && group.size() >= 3 && group.size() <= 50;
}

std::shared_ptr<User> Manager::findUser(const std::string& name) {
    const auto it = users_.find(name);
    if (it != users_.end()) return it->second;

    if (const auto user = db::query::identities::User::getUserByName(name)) {
        users_[name] = user;
        return user;
    }
    return nullptr;
}

std::string generateUUID() {
    uuid_t uuid;
    char uuidStr[37];
    uuid_generate(uuid);
    uuid_unparse(uuid, uuidStr);
    return {uuidStr};
}

std::shared_ptr<RefreshToken> Manager::createRefreshToken(const std::shared_ptr<ws::Session>& session) const {
    const auto now = std::chrono::system_clock::now();
    const auto exp = now + std::chrono::days(config::ConfigRegistry::get().auth.refresh_token_expiry_days);
    const std::string jti = generateUUID();

    const std::string token =
        jwt::create<jwt::traits::nlohmann_json>()
        .set_issuer("Vaulthalla")
        .set_subject(session->ipAddress + ":" + session->userAgent + ":" + session->uuid)
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_id(jti)
        .sign(jwt::algorithm::hs256{jwt_secret_});

    return RefreshToken::fromIssuedToken(
        jti,
        token,
        hash::password(token),
        0,
        session->userAgent,
        session->ipAddress
        );
}

}