#pragma once

#include "Vault.hpp"
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <optional>
#include "db/Fwd.hpp"

namespace vh::vault::model {

struct S3Vault : Vault {
    uint32_t api_key_id{};
    std::string bucket;
    std::optional<std::string> storage_tier_id;
    bool encrypt_upstream{true};

    S3Vault() = default;
    S3Vault(const std::string& name, uint32_t apiKeyID, std::string bucketName);
    explicit S3Vault(pqxx::row_ref row);
};

void to_json(nlohmann::json& j, const S3Vault& v);
void from_json(const nlohmann::json& j, S3Vault& v);

}
