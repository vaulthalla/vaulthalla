#pragma once

#include "identities/model/User.hpp"
#include "Token.hpp"
#include "RefreshToken.hpp"
#include "protocols/ws/Session.hpp"
#include "crypto/secrets/Manager.hpp"
#include <memory>
#include <string>

namespace vh::auth::model {

class Client {
  public:
    Client();

    Client(const std::shared_ptr<protocols::ws::Session>& session,
           const std::shared_ptr<RefreshToken>& refreshToken, const std::shared_ptr<identities::model::User>& user = nullptr);

    [[nodiscard]] std::shared_ptr<identities::model::User> getUser() const;
    [[nodiscard]] std::shared_ptr<Token> getToken() const;
    [[nodiscard]] std::shared_ptr<protocols::ws::Session> getSession() const;

    void setUser(const std::shared_ptr<identities::model::User>& user);
    void setToken(const std::shared_ptr<Token>& token);

    [[nodiscard]] std::string getUserName() const;
    [[nodiscard]] std::string getEmail() const;
    [[nodiscard]] std::string getRawToken() const;

    [[nodiscard]] bool expiresSoon() const;
    [[nodiscard]] bool isAuthenticated() const;

    void refreshToken();
    void invalidateToken() const;
    void closeConnection() const;

    void setSession(const std::shared_ptr<protocols::ws::Session>& session);

    void setRefreshToken(const std::shared_ptr<RefreshToken>& token) { refreshToken_ = token; }
    [[nodiscard]] const std::shared_ptr<RefreshToken>& getRefreshToken() const { return refreshToken_; }
    [[nodiscard]] std::string getHashedRefreshToken() const { return refreshToken_->getHashedToken(); }

    [[nodiscard]] bool validateToken(const std::string& token) const;

    void sendControlMessage(const std::string& type, const nlohmann::json& payload) const;

    [[nodiscard]] std::chrono::system_clock::time_point connOpenedAt() const { return openedAt_; }

  private:
    std::shared_ptr<identities::model::User> user_{nullptr};
    std::shared_ptr<Token> token_{nullptr};
    std::shared_ptr<protocols::ws::Session> session_;
    std::shared_ptr<RefreshToken> refreshToken_;
    const std::string jwt_secret_ = crypto::secrets::Manager().jwtSecret();
    std::chrono::system_clock::time_point openedAt_ = std::chrono::system_clock::now();

    [[nodiscard]] std::string generateToken(const std::string& name) const;
};

}
