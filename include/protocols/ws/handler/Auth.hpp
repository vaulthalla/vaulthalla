#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler {

using json = nlohmann::json;

struct Auth {
    static json login(const json& payload, const std::shared_ptr<Session>& session);
    static json registerUser(const json& payload, const std::shared_ptr<Session>& session);
    static json deleteUser(const json& payload, const std::shared_ptr<Session>& session);
    static json updateUser(const json& payload, const std::shared_ptr<Session>& session);
    static json changePassword(const json& payload, const std::shared_ptr<Session>& session);
    static json getUser(const json& payload, const std::shared_ptr<Session>& session);
    static json getUserByName(const json& payload, const std::shared_ptr<Session>& session);

    static json refreshToken(const std::string& token, const std::shared_ptr<Session>& session);
    static json isUserAuthenticated(const std::string& token, const std::shared_ptr<Session>& session);

    static json listUsers(const std::shared_ptr<Session>& session);
    static json logout(const std::shared_ptr<Session>& session);

    static json doesAdminHaveDefaultPassword();
};

}
