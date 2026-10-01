#pragma once

#include "ops/Error.hpp"

#include <memory>

namespace vh::identities { struct User; }

namespace vh::ops {

// The human principal an operation runs as. Both frontends already resolve to a User: the CLI from the peer's
// Linux UID, the ws router from an authenticated session. ops:: is the actor-authorized boundary; code beneath
// it (managers, db::query) is trusted and never authorizes. Internal callers use those primitives directly.
using Actor = std::shared_ptr<identities::User>;

// First line of every operation. A missing actor is a refusal, never a null dereference in the daemon.
inline void requireActor(const Actor& actor) {
    if (!actor) throw Denied("not authenticated");
}

}
