#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace vh::protocols::ws {

// True for object keys whose values are credentials or key material (passwords, tokens, secrets, API/access/
// private keys, cookies, authorization headers). Case-insensitive.
[[nodiscard]] bool isSensitiveLogKey(std::string_view key);

// Deep copy of `j` with every sensitive value (see isSensitiveLogKey) replaced by "[REDACTED]", for logging.
// Never log a raw ws message: login, password change, API key, and share-unlock payloads carry secrets.
[[nodiscard]] nlohmann::json redactForLog(const nlohmann::json& j);

}
