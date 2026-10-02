#pragma once

#include "ops/Actor.hpp"
#include "config/Config.hpp"

#include <nlohmann/json.hpp>

// Daemon settings writes, shared by ws settings.update / email.config.update, `vh email ...` and
// `vh s3-gateway enable|disable`. One validation (the web used to clamp or accept what the CLI refused, and
// settings.update skipped email validation entirely) and one apply step: saving, publishing to the registry, and
// restarting the S3 gateway when s3_gateway.enabled changes (settings.update used to flip the key and leave the
// gateway as it was).
namespace vh::ops::config {

using Config = vh::config::Config;

// The whole settings document (config::Config's JSON form) with the caller's edits applied. Super admin only.
Config saveSettings(const Actor& actor, const nlohmann::json& settings);

// `vh s3-gateway enable|disable`: admin.s3_gateway.manage_service.
Config setGatewayEnabled(const Actor& actor, bool enabled);

// Refuses an invalid document (throws ops::Invalid) without touching anything.
void validateSettings(const nlohmann::json& settings);

}
