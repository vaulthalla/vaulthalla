#include "share/Link.hpp"

#include "db/encoding/bytea.hpp"
#include "db/encoding/timestamp.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>

#include <sstream>

using namespace vh::db::encoding;

namespace vh::share {
namespace link_model_detail {
std::optional<std::time_t> opt_time(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return parsePostgresTimestamp(row[column].as<std::string>());
}

std::optional<std::string> opt_string(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<std::string>();
}

template <typename T>
std::optional<T> opt_num(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<T>();
}

std::vector<std::string> parse_pg_text_array(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return {};
    const auto raw = row[column].as<std::string>();
    if (raw.size() < 2 || raw.front() != '{' || raw.back() != '}') return {};
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (c == ',' && !quoted) {
            if (!current.empty()) out.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

nlohmann::json parse_metadata(const std::string& raw) {
    auto parsed = nlohmann::json::parse(raw.empty() ? "{}" : raw, nullptr, false);
    if (parsed.is_discarded()) return nlohmann::json::object();
    return parsed;
}
}

Link::Link(const pqxx::row& row)
    : id(row["id"].as<std::string>()),
      token_lookup_id(row["token_lookup_id"].as<std::string>()),
      token_hash(from_hex_bytea(row["token_hash"].as<std::string>())),
      created_by(row["created_by"].as<uint32_t>()),
      updated_by(link_model_detail::opt_num<uint32_t>(row, "updated_by")),
      revoked_by(link_model_detail::opt_num<uint32_t>(row, "revoked_by")),
      vault_id(row["vault_id"].as<uint32_t>()),
      root_entry_id(row["root_entry_id"].as<uint32_t>()),
      root_path(row["root_path"].as<std::string>()),
      target_type(target_type_from_string(row["target_type"].as<std::string>())),
      link_type(link_type_from_string(row["link_type"].as<std::string>())),
      access_mode(access_mode_from_string(row["access_mode"].as<std::string>())),
      allowed_ops(row["allowed_ops"].as<uint32_t>()),
      name(link_model_detail::opt_string(row, "name")),
      public_label(link_model_detail::opt_string(row, "public_label")),
      description(link_model_detail::opt_string(row, "description")),
      expires_at(link_model_detail::opt_time(row, "expires_at")),
      revoked_at(link_model_detail::opt_time(row, "revoked_at")),
      disabled_at(link_model_detail::opt_time(row, "disabled_at")),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      updated_at(parsePostgresTimestamp(row["updated_at"].as<std::string>())),
      last_accessed_at(link_model_detail::opt_time(row, "last_accessed_at")),
      access_count(row["access_count"].as<uint64_t>()),
      download_count(row["download_count"].as<uint64_t>()),
      upload_count(row["upload_count"].as<uint64_t>()),
      max_downloads(link_model_detail::opt_num<uint32_t>(row, "max_downloads")),
      max_upload_files(link_model_detail::opt_num<uint32_t>(row, "max_upload_files")),
      max_upload_size_bytes(link_model_detail::opt_num<uint64_t>(row, "max_upload_size_bytes")),
      max_upload_total_bytes(link_model_detail::opt_num<uint64_t>(row, "max_upload_total_bytes")),
      duplicate_policy(duplicate_policy_from_string(row["duplicate_policy"].as<std::string>())),
      allowed_mime_types(link_model_detail::parse_pg_text_array(row, "allowed_mime_types")),
      blocked_mime_types(link_model_detail::parse_pg_text_array(row, "blocked_mime_types")),
      allowed_extensions(link_model_detail::parse_pg_text_array(row, "allowed_extensions")),
      blocked_extensions(link_model_detail::parse_pg_text_array(row, "blocked_extensions")),
      metadata(row["metadata"].as<std::string>()) {}

Grant Link::grant() const {
    Grant g;
    g.vault_id = vault_id;
    g.root_entry_id = root_entry_id;
    g.root_path = root_path;
    g.target_type = target_type;
    g.allowed_ops = allowed_ops;
    g.max_downloads = max_downloads;
    g.max_upload_files = max_upload_files;
    g.max_upload_size_bytes = max_upload_size_bytes;
    g.max_upload_total_bytes = max_upload_total_bytes;
    g.duplicate_policy = duplicate_policy;
    g.allowed_mime_types = allowed_mime_types;
    g.blocked_mime_types = blocked_mime_types;
    g.allowed_extensions = allowed_extensions;
    g.blocked_extensions = blocked_extensions;
    return g;
}

bool Link::isExpired(const std::time_t now) const { return expires_at && *expires_at <= now; }
bool Link::isRevoked() const { return revoked_at.has_value(); }
bool Link::isDisabled() const { return disabled_at.has_value(); }
bool Link::isActive(const std::time_t now) const { return !isExpired(now) && !isRevoked() && !isDisabled(); }
bool Link::requiresEmail() const { return access_mode == AccessMode::EmailValidated; }

nlohmann::json Link::toManagementJson() const {
    return {
        {"id", id},
        {"token_lookup_id", token_lookup_id},
        {"created_by", created_by},
        {"updated_by", updated_by},
        {"revoked_by", revoked_by},
        {"vault_id", vault_id},
        {"root_entry_id", root_entry_id},
        {"root_path", root_path},
        {"target_type", target_type},
        {"link_type", link_type},
        {"access_mode", access_mode},
        {"allowed_ops", allowed_ops},
        {"name", name},
        {"public_label", public_label},
        {"description", description},
        {"expires_at", expires_at ? nlohmann::json(timestampToString(*expires_at)) : nlohmann::json(nullptr)},
        {"revoked_at", revoked_at ? nlohmann::json(timestampToString(*revoked_at)) : nlohmann::json(nullptr)},
        {"disabled_at", disabled_at ? nlohmann::json(timestampToString(*disabled_at)) : nlohmann::json(nullptr)},
        {"created_at", timestampToString(created_at)},
        {"updated_at", timestampToString(updated_at)},
        {"last_accessed_at", last_accessed_at ? nlohmann::json(timestampToString(*last_accessed_at)) : nlohmann::json(nullptr)},
        {"access_count", access_count},
        {"download_count", download_count},
        {"upload_count", upload_count},
        {"max_downloads", max_downloads},
        {"max_upload_files", max_upload_files},
        {"max_upload_size_bytes", max_upload_size_bytes},
        {"max_upload_total_bytes", max_upload_total_bytes},
        {"duplicate_policy", duplicate_policy},
        {"allowed_mime_types", allowed_mime_types},
        {"blocked_mime_types", blocked_mime_types},
        {"allowed_extensions", allowed_extensions},
        {"blocked_extensions", blocked_extensions},
        {"metadata", link_model_detail::parse_metadata(metadata)}
    };
}

nlohmann::json Link::toPublicJson() const {
    return {
        {"id", id},
        {"root_path", root_path},
        {"target_type", target_type},
        {"link_type", link_type},
        {"access_mode", access_mode},
        {"allowed_ops", allowed_ops},
        {"public_label", public_label},
        {"expires_at", expires_at ? nlohmann::json(timestampToString(*expires_at)) : nlohmann::json(nullptr)},
        {"metadata", link_model_detail::parse_metadata(metadata)}
    };
}

void to_json(nlohmann::json& j, const Link& link) { j = link.toManagementJson(); }

}
