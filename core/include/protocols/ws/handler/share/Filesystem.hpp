#pragma once

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include "protocols/ws/Fwd.hpp"
#include "share/Fwd.hpp"

namespace vh::protocols::ws::handler::share {

using json = nlohmann::json;

class Filesystem {
public:
    using ManagerFactory = std::function<std::shared_ptr<vh::share::Manager>()>;
    using ResolverFactory = std::function<std::shared_ptr<vh::share::TargetResolver>()>;

    static json metadata(const json& payload, const std::shared_ptr<Session>& session);
    static json list(const json& payload, const std::shared_ptr<Session>& session);

    static void setManagerFactoryForTesting(ManagerFactory factory);
    static void resetManagerFactoryForTesting();
    static void setResolverFactoryForTesting(ResolverFactory factory);
    static void resetResolverFactoryForTesting();
};

}
