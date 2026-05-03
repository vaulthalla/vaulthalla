#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

namespace vh::share {

enum class Operation : uint32_t {
    Metadata = 1u << 0,
    List = 1u << 1,
    Preview = 1u << 2,
    Download = 1u << 3,
    Upload = 1u << 4,
    Mkdir = 1u << 5,
    Overwrite = 1u << 6
};

enum class TargetType { File, Directory };
enum class LinkType { Download, Upload, Access };
enum class AccessMode { Public, EmailValidated };
enum class DuplicatePolicy { Reject, Rename, Overwrite };
enum class UploadStatus { Pending, Receiving, Complete, Failed, Cancelled };
enum class AuditActorType { OwnerUser, AdminUser, SharePrincipal, System, Unknown };
enum class AuditStatus { Success, Denied, Failed, RateLimited };

[[nodiscard]] uint32_t bit(Operation op);

[[nodiscard]] std::string to_string(Operation value);
[[nodiscard]] std::string to_string(TargetType value);
[[nodiscard]] std::string to_string(LinkType value);
[[nodiscard]] std::string to_string(AccessMode value);
[[nodiscard]] std::string to_string(DuplicatePolicy value);
[[nodiscard]] std::string to_string(UploadStatus value);
[[nodiscard]] std::string to_string(AuditActorType value);
[[nodiscard]] std::string to_string(AuditStatus value);

[[nodiscard]] Operation operation_from_string(std::string_view value);
[[nodiscard]] TargetType target_type_from_string(std::string_view value);
[[nodiscard]] LinkType link_type_from_string(std::string_view value);
[[nodiscard]] AccessMode access_mode_from_string(std::string_view value);
[[nodiscard]] DuplicatePolicy duplicate_policy_from_string(std::string_view value);
[[nodiscard]] UploadStatus upload_status_from_string(std::string_view value);
[[nodiscard]] AuditActorType audit_actor_type_from_string(std::string_view value);
[[nodiscard]] AuditStatus audit_status_from_string(std::string_view value);

void to_json(nlohmann::json& j, Operation value);
void to_json(nlohmann::json& j, TargetType value);
void to_json(nlohmann::json& j, LinkType value);
void to_json(nlohmann::json& j, AccessMode value);
void to_json(nlohmann::json& j, DuplicatePolicy value);
void to_json(nlohmann::json& j, UploadStatus value);
void to_json(nlohmann::json& j, AuditActorType value);
void to_json(nlohmann::json& j, AuditStatus value);

void from_json(const nlohmann::json& j, Operation& value);
void from_json(const nlohmann::json& j, TargetType& value);
void from_json(const nlohmann::json& j, LinkType& value);
void from_json(const nlohmann::json& j, AccessMode& value);
void from_json(const nlohmann::json& j, DuplicatePolicy& value);
void from_json(const nlohmann::json& j, UploadStatus& value);
void from_json(const nlohmann::json& j, AuditActorType& value);
void from_json(const nlohmann::json& j, AuditStatus& value);

}
