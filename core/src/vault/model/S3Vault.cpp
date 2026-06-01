#include "vault/model/S3Vault.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>
#include <optional>
#include <utility>

namespace vh::vault::model {

S3Vault::S3Vault(const std::string& name, const unsigned int apiKeyID, std::string bucketName)
    : Vault(), api_key_id(apiKeyID), bucket(std::move(bucketName)) {
    this->name = name;
    this->type = VaultType::S3;
    this->is_active = true;
    this->created_at = std::time(nullptr);
}

S3Vault::S3Vault(const pqxx::row& row)
    : Vault(row),
      api_key_id(row["api_key_id"].as<std::optional<unsigned int>>().value_or(0)),
      bucket(row["bucket"].as<std::optional<std::string>>().value_or("")),
      storage_tier_id(row["storage_tier_id"].as<std::optional<std::string>>()),
      encrypt_upstream(row["encrypt_upstream"].as<std::optional<bool>>().value_or(true)) {}

void to_json(nlohmann::json& j, const S3Vault& v) {
    to_json(j, static_cast<const Vault&>(v));
    j["api_key_id"] = v.api_key_id;
    j["bucket"] = v.bucket;
    j["storage_tier_id"] = v.storage_tier_id ? nlohmann::json(*v.storage_tier_id) : nlohmann::json(nullptr);
    j["encrypt_upstream"] = v.encrypt_upstream;
}

void from_json(const nlohmann::json& j, S3Vault& v) {
    from_json(j, static_cast<Vault&>(v));
    v.api_key_id = j.at("api_key_id").get<unsigned int>();
    v.bucket = j.at("bucket").get<std::string>();
    if (j.contains("storage_tier_id") && !j.at("storage_tier_id").is_null())
        v.storage_tier_id = j.at("storage_tier_id").get<std::string>();
    else
        v.storage_tier_id = std::nullopt;
    v.encrypt_upstream = j.value("encrypt_upstream", true);
}

}
