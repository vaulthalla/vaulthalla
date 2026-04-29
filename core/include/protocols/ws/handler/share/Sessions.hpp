#pragma once

#include <functional>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::protocols::ws { class Session; }
namespace vh::share { class Manager; }

namespace vh::protocols::ws::handler::share {

using json = nlohmann::json;

struct Sessions {
    using ManagerFactory = std::function<std::shared_ptr<vh::share::Manager>()>;

    static json open(const json& payload, const std::shared_ptr<Session>& session);
    static json startEmailChallenge(const json& payload, const std::shared_ptr<Session>& session);
    static json confirmEmailChallenge(const json& payload, const std::shared_ptr<Session>& session);

    static void setManagerFactoryForTesting(ManagerFactory factory);
    static void resetManagerFactoryForTesting();
};

}
