#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/asio.hpp>

namespace vh::protocols::ws { class Session; class Router; }
namespace vh::identities::model { struct User; }

namespace vh::auth::session {

class Manager {
public:
    void accept(boost::asio::ip::tcp::socket&& socket, const std::shared_ptr<protocols::ws::Router>& router);
    void tryRehydrate(const std::shared_ptr<protocols::ws::Session>& session);

    void promote(const std::shared_ptr<protocols::ws::Session>& session);
    void cache(const std::shared_ptr<protocols::ws::Session>& session);
    void renewAccessToken(const std::shared_ptr<protocols::ws::Session>& session, const std::string& existingToken);

    bool validate(const std::shared_ptr<protocols::ws::Session>& session, const std::string& accessToken);
    std::shared_ptr<protocols::ws::Session> validateRawRefreshToken(const std::string& refreshToken);
    void invalidate(const std::string& token);
    void invalidate(const std::shared_ptr<protocols::ws::Session>& session);

    std::shared_ptr<protocols::ws::Session> get(const std::string& token);
    std::vector<std::shared_ptr<protocols::ws::Session>> getSessions(const std::shared_ptr<identities::model::User>& user);
    std::vector<std::shared_ptr<protocols::ws::Session>> getSessionsByUserId(uint32_t userId);

    std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>> getActive();

private:
    std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>> sessionsByUUID_;
    std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>> sessionsByRefreshJti_;
    std::unordered_multimap<uint32_t, std::shared_ptr<protocols::ws::Session>> sessionsByUserId_;
    std::mutex sessionMutex_;
};

inline std::string to_string(
    const std::unordered_map<std::string, std::shared_ptr<protocols::ws::Session>>& sessions
) {
    std::string result;
    for (const auto& [token, session] : sessions)
        result += "Token: " + token + "\n";
    return result;
}

}
