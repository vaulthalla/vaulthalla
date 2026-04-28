#include "share/Upload.hpp"

#include "db/encoding/timestamp.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>

using namespace vh::db::encoding;

namespace vh::share {
namespace upload_model_detail {
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
}

Upload::Upload(const pqxx::row& row)
    : id(row["id"].as<std::string>()),
      share_id(row["share_id"].as<std::string>()),
      share_session_id(row["share_session_id"].as<std::string>()),
      target_parent_entry_id(row["target_parent_entry_id"].as<uint32_t>()),
      target_path(row["target_path"].as<std::string>()),
      tmp_path(upload_model_detail::opt_string(row, "tmp_path")),
      original_filename(row["original_filename"].as<std::string>()),
      resolved_filename(row["resolved_filename"].as<std::string>()),
      expected_size_bytes(row["expected_size_bytes"].as<uint64_t>()),
      received_size_bytes(row["received_size_bytes"].as<uint64_t>()),
      mime_type(upload_model_detail::opt_string(row, "mime_type")),
      content_hash(upload_model_detail::opt_string(row, "content_hash")),
      created_entry_id(upload_model_detail::opt_num<uint32_t>(row, "created_entry_id")),
      status(upload_status_from_string(row["status"].as<std::string>())),
      error(upload_model_detail::opt_string(row, "error")),
      started_at(parsePostgresTimestamp(row["started_at"].as<std::string>())),
      completed_at(upload_model_detail::opt_time(row, "completed_at")) {}

bool Upload::isTerminal() const {
    return status == UploadStatus::Complete || status == UploadStatus::Failed || status == UploadStatus::Cancelled;
}

bool Upload::exceedsExpectedSize(const uint64_t next_bytes) const {
    return next_bytes > expected_size_bytes - std::min(received_size_bytes, expected_size_bytes);
}

void to_json(nlohmann::json& j, const Upload& upload) {
    j = {
        {"id", upload.id},
        {"share_id", upload.share_id},
        {"share_session_id", upload.share_session_id},
        {"target_parent_entry_id", upload.target_parent_entry_id},
        {"target_path", upload.target_path},
        {"original_filename", upload.original_filename},
        {"resolved_filename", upload.resolved_filename},
        {"expected_size_bytes", upload.expected_size_bytes},
        {"received_size_bytes", upload.received_size_bytes},
        {"mime_type", upload.mime_type},
        {"content_hash", upload.content_hash},
        {"created_entry_id", upload.created_entry_id},
        {"status", upload.status},
        {"error", upload.error},
        {"started_at", timestampToString(upload.started_at)},
        {"completed_at", upload.completed_at ? nlohmann::json(timestampToString(*upload.completed_at)) : nlohmann::json(nullptr)}
    };
}

}
