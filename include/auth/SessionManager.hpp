#pragma once

#include "model/Client.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vh::auth {

class SessionManager {
  public:
    void createSession(const std::shared_ptr<model::Client>& client);
    std::string promoteSession(const std::shared_ptr<model::Client>& client);
    std::shared_ptr<model::Client> getClientSession(const std::string& token);
    void invalidateSession(const std::string& token);
    void invalidateUserSession(uint32_t userId);

    // For admin / debug: list active sessions
    std::unordered_map<std::string, std::shared_ptr<model::Client>> getActiveSessions();

  private:
    std::unordered_map<std::string, std::shared_ptr<model::Client>> sessionsByUUID_;
    std::unordered_map<std::string, std::shared_ptr<model::Client>> sessionsByRefreshJti_;
    std::unordered_map<uint32_t, std::shared_ptr<model::Client>> sessionsByUserId_;
    std::mutex sessionMutex_;
};

inline std::string to_string(const std::unordered_map<std::string, std::shared_ptr<model::Client>>& sessions) {
    std::string result;
    for (const auto& [token, client] : sessions) result += "Token: " + token + "\n";
    return result;
}

} // namespace vh::auth
