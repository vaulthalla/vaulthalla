#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vh::protocols::ws { struct Session; }

namespace vh::auth::session {

class Manager {
  public:
    void ensureSession(const std::shared_ptr<protocols::ws::Session>& session);
    std::string promoteSession(const std::shared_ptr<protocols::ws::Session>& session);
    std::shared_ptr<protocols::ws::Session> getSession(const std::string& token);
    void invalidateSession(const std::string& token);

    // For admin / debug: list active sessions
    std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>> getActiveSessions();

  private:
    std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>> sessionsByUUID_;
    std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>> sessionsByRefreshJti_;
    std::unordered_map<uint32_t, std::shared_ptr<protocols::ws::Session>> sessionsByUserId_;
    std::mutex sessionMutex_;
};

inline std::string to_string(const std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>>& sessions) {
    std::string result;
    for (const auto& [token, session] : sessions) result += "Token: " + token + "\n";
    return result;
}

} // namespace vh::auth
