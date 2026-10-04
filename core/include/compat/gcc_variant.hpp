#pragma once

// Force-included first in every translation unit when building with GCC (see the root meson.build).
//
// GCC 14 at -O3 reports -Wmaybe-uninitialized inside libstdc++'s std::variant move constructor: a variant holding its
// empty std::nullptr_t alternative has no initialized bytes, and moving it "reads" them. libpqxx stores every query
// parameter in such a variant (pqxx::params), so the false positive surfaces in whichever translation unit happens to
// inline the move, dozens of them in a non-unity build. GCC checks the pragma state at the warning's location, which
// is <variant>'s own text, and only the first inclusion of a header counts; including it here, first, silences the
// warning for that header's code alone. Project code keeps -Wmaybe-uninitialized.

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <variant>
#pragma GCC diagnostic pop
#endif
