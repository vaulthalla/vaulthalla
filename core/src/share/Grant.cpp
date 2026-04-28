#include "share/Grant.hpp"

#include <nlohmann/json.hpp>

namespace vh::share {

bool Grant::allows(const Operation op) const { return (allowed_ops & bit(op)) != 0; }

void Grant::requireValid() const {
    if (vault_id == 0) throw std::invalid_argument("Share grant vault_id is required");
    if (root_entry_id == 0) throw std::invalid_argument("Share grant root_entry_id is required");
    if (root_path.empty() || root_path.front() != '/') throw std::invalid_argument("Share grant root_path must be absolute");
    if (target_type == TargetType::File && allows(Operation::Mkdir))
        throw std::invalid_argument("Share file target cannot allow mkdir");
    if (duplicate_policy == DuplicatePolicy::Overwrite && !allows(Operation::Overwrite))
        throw std::invalid_argument("Share overwrite duplicate policy requires overwrite permission");
}

nlohmann::json Grant::toManagementJson() const {
    return {
        {"vault_id", vault_id},
        {"root_entry_id", root_entry_id},
        {"root_path", root_path},
        {"target_type", target_type},
        {"allowed_ops", allowed_ops},
        {"max_downloads", max_downloads},
        {"max_upload_files", max_upload_files},
        {"max_upload_size_bytes", max_upload_size_bytes},
        {"max_upload_total_bytes", max_upload_total_bytes},
        {"duplicate_policy", duplicate_policy},
        {"allowed_mime_types", allowed_mime_types},
        {"blocked_mime_types", blocked_mime_types},
        {"allowed_extensions", allowed_extensions},
        {"blocked_extensions", blocked_extensions}
    };
}

nlohmann::json Grant::toPublicJson() const {
    return {
        {"root_path", root_path},
        {"target_type", target_type},
        {"allowed_ops", allowed_ops},
        {"max_downloads", max_downloads},
        {"max_upload_files", max_upload_files},
        {"max_upload_size_bytes", max_upload_size_bytes},
        {"max_upload_total_bytes", max_upload_total_bytes},
        {"duplicate_policy", duplicate_policy}
    };
}

void to_json(nlohmann::json& j, const Grant& grant) { j = grant.toManagementJson(); }

void from_json(const nlohmann::json& j, Grant& grant) {
    grant.vault_id = j.at("vault_id").get<uint32_t>();
    grant.root_entry_id = j.at("root_entry_id").get<uint32_t>();
    grant.root_path = j.at("root_path").get<std::string>();
    grant.target_type = j.at("target_type").get<TargetType>();
    grant.allowed_ops = j.at("allowed_ops").get<uint32_t>();
    if (j.contains("max_downloads") && !j["max_downloads"].is_null()) grant.max_downloads = j["max_downloads"].get<uint32_t>();
    if (j.contains("max_upload_files") && !j["max_upload_files"].is_null()) grant.max_upload_files = j["max_upload_files"].get<uint32_t>();
    if (j.contains("max_upload_size_bytes") && !j["max_upload_size_bytes"].is_null()) grant.max_upload_size_bytes = j["max_upload_size_bytes"].get<uint64_t>();
    if (j.contains("max_upload_total_bytes") && !j["max_upload_total_bytes"].is_null()) grant.max_upload_total_bytes = j["max_upload_total_bytes"].get<uint64_t>();
    if (j.contains("duplicate_policy")) grant.duplicate_policy = j["duplicate_policy"].get<DuplicatePolicy>();
    if (j.contains("allowed_mime_types")) grant.allowed_mime_types = j["allowed_mime_types"].get<std::vector<std::string>>();
    if (j.contains("blocked_mime_types")) grant.blocked_mime_types = j["blocked_mime_types"].get<std::vector<std::string>>();
    if (j.contains("allowed_extensions")) grant.allowed_extensions = j["allowed_extensions"].get<std::vector<std::string>>();
    if (j.contains("blocked_extensions")) grant.blocked_extensions = j["blocked_extensions"].get<std::vector<std::string>>();
}

}
