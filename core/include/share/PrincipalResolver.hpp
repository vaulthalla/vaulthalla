#pragma once

#include "share/Link.hpp"
#include "share/Principal.hpp"
#include "share/Session.hpp"

#include <ctime>
#include <memory>

namespace vh::share {

struct PrincipalResolutionOptions {
    std::time_t now{};
};

class PrincipalResolver {
public:
    static std::shared_ptr<Principal> resolve(
        const Link& link,
        const Session& session,
        const PrincipalResolutionOptions& options
    );

    static std::shared_ptr<Principal> resolve(
        const Link& link,
        const Session& session,
        std::time_t now
    );
};

}
