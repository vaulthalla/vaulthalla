#pragma once

#include "share/Types.hpp"

#include <optional>
#include <string>

namespace vh::config { struct SharingConfig; }

// The operator's sharing.* switches (#164). share::Manager checks them when a link is created or updated and every
// time one is opened or resolved, and share::TargetResolver checks them on every access, so a switch turned off in
// config (or the console) stops links that already have live sessions too. Management (list, get, revoke, rotate)
// is never gated, so links can still be cleaned up while sharing is off.
//
// Mapping onto the link model (share_link.access_mode):
//   sharing.enabled                 every link
//   sharing.enable_anonymous        access_mode "public": anyone holding the URL
//   sharing.enable_email_validated  access_mode "email_validated": the recipient verifies an invited address
namespace vh::share::policy {

// Why links of this mode are refused under `sharing`, or nullopt when they are allowed. No mode: the master switch only.
[[nodiscard]] std::optional<std::string> refusal(const config::SharingConfig& sharing,
                                                 std::optional<AccessMode> mode = std::nullopt);

// The same against the live config::Registry; throws ops::Denied (ws code "denied") with the refusal text.
void requireEnabled(std::optional<AccessMode> mode = std::nullopt);

}
