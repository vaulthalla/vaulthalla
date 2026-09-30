#pragma once

#include <string_view>

// Server-side enforcement of the seeded admin password change (issue #103). The seed keeps a deterministic
// default password for the web `admin` account on purpose; until it is changed, an authenticated session may
// only change its password, check/refresh/end its session, and read what the change-password page needs.
// Previously this was enforced only by the browser (RequireAuth.tsx), so any ws client could skip it.
namespace vh::protocols::ws::default_password {

// Must match the seeded value (core/seed/src/seed_db.cpp) and db::query::identities::User::adminPasswordIsDefault().
inline constexpr std::string_view kSeededAdminPassword = "vh!adm1n";

inline constexpr std::string_view kErrorCode = "password_change_required";
inline constexpr std::string_view kErrorMessage =
    "Password change required: this account still uses the default password. "
    "Change it (auth.user.change_password) before using any other command.";

[[nodiscard]] bool isAllowedWhileDefault(std::string_view command);

}
