#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "protocols/shell/Table.hpp"
#include "db/query/vault/Vault.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>
#include <fmt/core.h>

using namespace vh::vault::model;
using namespace vh::protocols::shell;
using namespace vh::db::encoding;

std::string Vault::quotaStr() const {
    if (quota == 0) return "unlimited";
    if (quota < 1024) return fmt::format("{}B", static_cast<unsigned long long>(quota));
    if (quota < 1024 * 1024) return fmt::format("{:.2f}K", static_cast<double>(quota) / 1024);
    if (quota < 1024 * 1024 * 1024) return fmt::format("{:.2f}M", static_cast<double>(quota) / (1024 * 1024));
    if (quota < 1024ull * 1024 * 1024 * 1024) return fmt::format("{:.2f}G", static_cast<double>(quota) / (1024 * 1024 * 1024));
    return fmt::format("{:.2f}T", static_cast<double>(quota) / (1024ull * 1024 * 1024 * 1024));
}

void Vault::setQuotaFromStr(const std::string& str) {
    const auto identifier = str.back();
    if (str == "unlimited") {
        quota = 0;
        return;
    }
    try {
        if (identifier == 'B' || identifier == 'b') quota = std::stoull(str.substr(0, str.size() - 1));
        else if (identifier == 'K' || identifier == 'k')
            quota = static_cast<uintmax_t>(std::stoull(str.substr(0, str.size() - 1)) * 1024ULL);
        else if (identifier == 'M' || identifier == 'm')
            quota = static_cast<uintmax_t>(std::stoull(str.substr(0, str.size() - 1)) * 1024ULL * 1024ULL);
        else if (identifier == 'G' || identifier == 'g')
            quota = static_cast<uintmax_t>(std::stoull(str.substr(0, str.size() - 1)) * 1024ULL * 1024ULL * 1024ULL);
        else if (identifier == 'T' || identifier == 't')
            quota = static_cast<uintmax_t>(std::stoull(str.substr(0, str.size() - 1)) * 1024ULL * 1024ULL * 1024ULL * 1024ULL);
        else quota = std::stoull(str);
    } catch (const std::exception& e) {
        throw std::invalid_argument("Invalid quota string: " + str);
    }
}


std::string vh::vault::model::to_string(const VaultType& type) {
    switch (type) {
        case VaultType::Local: return "local";
        case VaultType::S3: return "s3";
        default: return "unknown";
    }
}

VaultType vh::vault::model::from_string(const std::string& type) {
    if (type == "local") return VaultType::Local;
    if (type == "s3") return VaultType::S3;
    throw std::invalid_argument("Invalid VaultType: " + type);
}

Vault::Vault(const pqxx::row& row)
    : id(row["id"].as<unsigned int>()),
      owner_id(row["owner_id"].as<unsigned int>()),
      name(row["name"].as<std::string>()),
      description(row["description"].as<std::string>()),
      quota(row["quota"].as<unsigned long long>()),
      type(from_string(row["type"].as<std::string>())),
      mount_point(std::filesystem::path(row["mount_point"].as<std::string>())),
      allow_fs_write(row["allow_fs_write"].as<bool>()),
      is_active(row["is_active"].as<bool>()),
      created_at(parsePostgresTimestamp(row["created_at"].c_str())) {}

void vh::vault::model::to_json(nlohmann::json& j, const Vault& v) {
    j = {
        {"id", v.id},
        {"name", v.name},
        {"type", to_string(v.type)},
        {"description", v.description},
        {"quota", v.quota},
        {"owner_id", v.owner_id},
        {"mount_point", v.mount_point.string()},
        {"allow_fs_write", v.allow_fs_write},
        {"is_active", v.is_active},
        {"created_at", timestampToString(v.created_at)}
    };
}

void vh::vault::model::from_json(const nlohmann::json& j, Vault& v) {
    v.id = j.at("id").get<unsigned int>();
    v.name = j.at("name").get<std::string>();
    v.description = j.at("description").get<std::string>();
    v.quota = j.at("quota").get<unsigned long long>();
    v.type = from_string(j.at("type").get<std::string>());
    v.owner_id = j.at("owner_id").get<unsigned int>();
    v.mount_point = std::filesystem::path(j.at("mount_point").get<std::string>());
    if (j.contains("allow_fs_write")) v.allow_fs_write = j.at("allow_fs_write").get<bool>();
    v.is_active = j.at("is_active").get<bool>();
    v.created_at = parseTimestampFromString(j.at("created_at").get<std::string>());
}

void vh::vault::model::to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Vault>>& vaults) {
    j = nlohmann::json::array();
    for (const auto& vault : vaults) {
        if (const auto* s3 = dynamic_cast<const S3Vault*>(vault.get())) j.push_back(*s3);
        else j.push_back(*vault);
    }
}

std::string vh::vault::model::to_string(const Vault& v) {
    std::string out = "Name: " + v.name + "\n";
    out += "ID: " + std::to_string(v.id) + "\n";
    out += "Owner ID: " + std::to_string(v.owner_id) + "\n";
    out += "Type: " + to_string(v.type) + "\n";
    out += "Description: " + v.description + "\n";
    out += "Mount Point: " + v.mount_point.string() + "\n";
    out += "Quota: ";
    if (v.quota == 0) out += "\u221E\n";  // ∞ symbol
    else out += fmt::format("{} ({} bytes)\n", human_bytes(v.quota), static_cast<unsigned long long>(v.quota));
    out += "Created At: " + timestampToString(v.created_at) + "\n";
    out += "Allow FS Write: " + std::string(v.allow_fs_write ? "true" : "false") + "\n";
    out += "Is Active: " + std::string(v.is_active ? "true" : "false") + "\n";
    return out;
}

std::string vh::vault::model::to_string(const std::shared_ptr<Vault>& v) {
    if (!v) return "null";
    return to_string(*v);
}


std::string vh::vault::model::to_string(const std::vector<std::shared_ptr<Vault>>& vaults) {

    Table tbl({
        {"ID",    Align::Right, 3, 8,   false, false },
        { "OWNER", Align::Right, 4, 16,  false, false },
        { "NAME",  Align::Left,  4, 64,  false, false },
        { "TYPE",  Align::Left,  4, 24,  false, false },
        { "QUOTA", Align::Right, 5, 32,  false, false },
        // description wraps, effectively acts as flex column
        { "DESCRIPTION", Align::Left, 11, 2000, true, false },
    }, /*term_width*/ term_width());

    if (vaults.empty()) {
        auto out = tbl.render();
        out += "\n";
        out += "No vaults found\n";
        return out;
    }

    for (const auto& vp : vaults) {
        const auto& v = *vp;
        const std::string quota = (v.quota == 0)
            ? "∞"
            : fmt::format("{} ({})",
                          human_bytes(static_cast<uint64_t>(v.quota)),
                          static_cast<unsigned long long>(v.quota));

        std::string owner = v.owner_id != 0 ?
            db::query::vault::Vault::getVaultOwnersName(v.id) + " (ID: " + std::to_string(v.owner_id) + ")" : "N/A";

        tbl.add_row({
            std::to_string(v.id),
            owner,
            v.name,
            to_string(v.type),
            quota,
            v.description
        });
    }

    std::string out;
    out += "vaulthalla vaults:\n";
    out += tbl.render();
    return out;
}
