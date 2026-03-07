#pragma once
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::auth::session {

struct Validator {
    [[nodiscard]] static bool validateAccessToken(const std::shared_ptr<protocols::ws::Session>& session, const std::string& accessToken);
    [[nodiscard]] static bool validateSession(const std::shared_ptr<protocols::ws::Session>& session);
};

}
