#include "protocols/ws/DefaultPasswordGate.hpp"

#include <algorithm>
#include <array>

namespace vh::protocols::ws::default_password {

bool isAllowedWhileDefault(const std::string_view command) {
    static constexpr std::array<std::string_view, 8> kAllowed{
        "auth.login",
        "auth.logout",
        "auth.refresh",
        "auth.isAuthenticated",
        "auth.user.change_password",
        "auth.admin.default_password",  // RequireAuth reads this to route to the change-password page
        "auth.user.get",                // the change-password page loads the account it is changing
        "auth.user.get.byName",
    };
    return std::ranges::find(kAllowed, command) != kAllowed.end();
}

}
