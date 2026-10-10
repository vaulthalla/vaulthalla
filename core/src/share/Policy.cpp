#include "share/Policy.hpp"

#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "ops/Error.hpp"

namespace vh::share::policy {

std::optional<std::string> refusal(const config::SharingConfig& sharing, const std::optional<AccessMode> mode) {
    if (!sharing.enabled) return "Sharing is disabled on this server";
    if (!mode) return std::nullopt;
    switch (*mode) {
        case AccessMode::Public:
            if (!sharing.enable_anonymous) return "Anonymous share links are disabled on this server";
            return std::nullopt;
        case AccessMode::EmailValidated:
            if (!sharing.enable_email_validated) return "Email-verified share links are disabled on this server";
            return std::nullopt;
    }
    return "Unknown share access mode";
}

void requireEnabled(const std::optional<AccessMode> mode) {
    if (auto reason = refusal(config::Registry::get().sharing, mode)) throw ops::Denied(*reason);
}

}
