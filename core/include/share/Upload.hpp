#pragma once

#include "share/Types.hpp"

#include <ctime>
#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::share {

struct Upload {
    std::string id;
    std::string share_id;
    std::string share_session_id;
    uint32_t target_parent_entry_id{};
    std::string target_path;
    std::optional<std::string> tmp_path;
    std::string original_filename;
    std::string resolved_filename;
    uint64_t expected_size_bytes{};
    uint64_t received_size_bytes{};
    std::optional<std::string> mime_type;
    std::optional<std::string> content_hash;
    std::optional<uint32_t> created_entry_id;
    UploadStatus status{UploadStatus::Pending};
    std::optional<std::string> error;
    std::time_t started_at{};
    std::optional<std::time_t> completed_at;

    Upload() = default;
    explicit Upload(const pqxx::row& row);

    [[nodiscard]] bool isTerminal() const;
    [[nodiscard]] bool exceedsExpectedSize(uint64_t next_bytes) const;
};

void to_json(nlohmann::json& j, const Upload& upload);

}
