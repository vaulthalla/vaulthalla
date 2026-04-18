#include "vault/model/S3Vault.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>
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
      api_key_id(row["api_key_id"].as<unsigned int>()),
      bucket(row["bucket"].as<std::string>()),
      encrypt_upstream(row["encrypt_upstream"].as<bool>()) {}

void to_json(nlohmann::json& j, const S3Vault& v) {
    to_json(j, static_cast<const Vault&>(v));
    j["api_key_id"] = v.api_key_id;
    j["bucket"] = v.bucket;
    j["encrypt_upstream"] = v.encrypt_upstream;
}

void from_json(const nlohmann::json& j, S3Vault& v) {
    from_json(j, static_cast<Vault&>(v));
    v.api_key_id = j.at("api_key_id").get<unsigned int>();
    v.bucket = j.at("bucket").get<std::string>();
    v.encrypt_upstream = j.value("encrypt_upstream", true);
}

}
