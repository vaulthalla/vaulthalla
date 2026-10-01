#pragma once

#include <stdexcept>

// Refusals raised by ops:: operations. They are what crosses the shared boundary: the CLI adapter turns them into
// a usage-error result (exit 2), the ws handler templates into an ERROR response. Anything that is not an
// ops::Error is a fault, not a refusal, and keeps propagating as before.
namespace vh::ops {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// RBAC or a business invariant refused the actor.
struct Denied : Error {
    using Error::Error;
};

struct NotFound : Error {
    using Error::Error;
};

// The request itself is malformed (bad name, out-of-range value).
struct Invalid : Error {
    using Error::Error;
};

// The request collides with existing state (name or id already taken).
struct Conflict : Error {
    using Error::Error;
};

}
