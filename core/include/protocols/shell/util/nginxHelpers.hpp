#pragma once

#include <filesystem>

namespace vh::protocols::shell::util {

inline constexpr auto* kNginxSiteAvailable = "/etc/nginx/sites-available/vaulthalla";
inline constexpr auto* kNginxSiteEnabled = "/etc/nginx/sites-enabled/vaulthalla";
inline constexpr auto* kNginxManagedMarker = "/var/lib/vaulthalla/nginx_site_managed";

bool isManagedSiteSymlinkTarget(const std::filesystem::path& linkPath);

}
