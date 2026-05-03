#include "rbac/Actor.hpp"

#include <stdexcept>
#include <utility>

namespace vh::rbac {

Actor Actor::human(std::shared_ptr<identities::User> user) {
    if (!user) throw std::invalid_argument("rbac actor human user is required");

    Actor actor{Kind::Human};
    actor.user_ = std::move(user);
    return actor;
}

Actor Actor::share(std::shared_ptr<vh::share::Principal> principal) {
    if (!principal) throw std::invalid_argument("rbac actor share principal is required");

    Actor actor{Kind::Share};
    actor.principal_ = std::move(principal);
    return actor;
}

}
