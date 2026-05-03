#pragma once

#include "share/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace vh::share {

struct Grant {
    uint32_t vault_id{};
    uint32_t root_entry_id{};
    std::string root_path{"/"};
    TargetType target_type{TargetType::Directory};
    uint32_t allowed_ops{};
    std::optional<uint32_t> max_downloads;
    std::optional<uint32_t> max_upload_files;
    std::optional<uint64_t> max_upload_size_bytes;
    std::optional<uint64_t> max_upload_total_bytes;
    DuplicatePolicy duplicate_policy{DuplicatePolicy::Reject};
    std::vector<std::string> allowed_mime_types;
    std::vector<std::string> blocked_mime_types;
    std::vector<std::string> allowed_extensions;
    std::vector<std::string> blocked_extensions;

    [[nodiscard]] bool allows(Operation op) const;
    void requireValid() const;
    [[nodiscard]] nlohmann::json toManagementJson() const;
    [[nodiscard]] nlohmann::json toPublicJson() const;
};

void to_json(nlohmann::json& j, const Grant& grant);
void from_json(const nlohmann::json& j, Grant& grant);

}
