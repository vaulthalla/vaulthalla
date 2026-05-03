#pragma once

#include "share/Grant.hpp"

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::share {

struct Link {
    std::string id;
    std::string token_lookup_id;
    std::vector<uint8_t> token_hash;
    uint32_t created_by{};
    std::optional<uint32_t> updated_by;
    std::optional<uint32_t> revoked_by;
    uint32_t vault_id{};
    uint32_t root_entry_id{};
    std::string root_path{"/"};
    TargetType target_type{TargetType::Directory};
    LinkType link_type{LinkType::Access};
    AccessMode access_mode{AccessMode::Public};
    uint32_t allowed_ops{};
    std::optional<std::string> name;
    std::optional<std::string> public_label;
    std::optional<std::string> description;
    std::optional<std::time_t> expires_at;
    std::optional<std::time_t> revoked_at;
    std::optional<std::time_t> disabled_at;
    std::time_t created_at{};
    std::time_t updated_at{};
    std::optional<std::time_t> last_accessed_at;
    uint64_t access_count{};
    uint64_t download_count{};
    uint64_t upload_count{};
    std::optional<uint32_t> max_downloads;
    std::optional<uint32_t> max_upload_files;
    std::optional<uint64_t> max_upload_size_bytes;
    std::optional<uint64_t> max_upload_total_bytes;
    DuplicatePolicy duplicate_policy{DuplicatePolicy::Reject};
    std::vector<std::string> allowed_mime_types;
    std::vector<std::string> blocked_mime_types;
    std::vector<std::string> allowed_extensions;
    std::vector<std::string> blocked_extensions;
    std::string metadata{"{}"};

    Link() = default;
    explicit Link(const pqxx::row& row);

    [[nodiscard]] Grant grant() const;
    [[nodiscard]] bool isExpired(std::time_t now) const;
    [[nodiscard]] bool isRevoked() const;
    [[nodiscard]] bool isDisabled() const;
    [[nodiscard]] bool isActive(std::time_t now) const;
    [[nodiscard]] bool requiresEmail() const;
    [[nodiscard]] nlohmann::json toManagementJson() const;
    [[nodiscard]] nlohmann::json toPublicJson() const;
};

void to_json(nlohmann::json& j, const Link& link);

}
