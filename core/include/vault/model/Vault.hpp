#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <filesystem>

namespace pqxx {
class row;
}

namespace vh::vault::model {

enum class VaultType { Local, S3 };

std::string to_string(const VaultType& type);
VaultType from_string(const std::string& type);

struct Vault {
    uint32_t id{}, owner_id{};
    std::string name, description{};
    uintmax_t quota{};
    VaultType type{VaultType::Local};
    std::filesystem::path mount_point;
    bool allow_fs_write{false};
    bool is_active{true};
    std::time_t created_at{};

    Vault() = default;
    virtual ~Vault() = default;
    explicit Vault(const pqxx::row& row);

    [[nodiscard]] std::string quotaStr() const;
    void setQuotaFromStr(const std::string& str);
};

void to_json(nlohmann::json& j, const Vault& v);
void from_json(const nlohmann::json& j, Vault& v);

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Vault>>& vaults);

std::string to_string(const Vault& v);
std::string to_string(const std::shared_ptr<Vault>& v);
std::string to_string(const std::vector<std::shared_ptr<Vault>>& vaults);

}
