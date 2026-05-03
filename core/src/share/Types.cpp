#include "share/Types.hpp"

#include <nlohmann/json.hpp>

namespace vh::share {

uint32_t bit(const Operation op) { return static_cast<uint32_t>(op); }

namespace {
template <typename E>
E fail_enum(const std::string_view value) {
    throw std::invalid_argument("Invalid share enum value: " + std::string(value));
}
}

std::string to_string(const Operation value) {
    switch (value) {
        case Operation::Metadata: return "metadata";
        case Operation::List: return "list";
        case Operation::Preview: return "preview";
        case Operation::Download: return "download";
        case Operation::Upload: return "upload";
        case Operation::Mkdir: return "mkdir";
        case Operation::Overwrite: return "overwrite";
    }
    throw std::invalid_argument("Invalid share operation");
}

std::string to_string(const TargetType value) {
    switch (value) {
        case TargetType::File: return "file";
        case TargetType::Directory: return "directory";
    }
    throw std::invalid_argument("Invalid share target type");
}

std::string to_string(const LinkType value) {
    switch (value) {
        case LinkType::Download: return "download";
        case LinkType::Upload: return "upload";
        case LinkType::Access: return "access";
    }
    throw std::invalid_argument("Invalid share link type");
}

std::string to_string(const AccessMode value) {
    switch (value) {
        case AccessMode::Public: return "public";
        case AccessMode::EmailValidated: return "email_validated";
    }
    throw std::invalid_argument("Invalid share access mode");
}

std::string to_string(const DuplicatePolicy value) {
    switch (value) {
        case DuplicatePolicy::Reject: return "reject";
        case DuplicatePolicy::Rename: return "rename";
        case DuplicatePolicy::Overwrite: return "overwrite";
    }
    throw std::invalid_argument("Invalid share duplicate policy");
}

std::string to_string(const UploadStatus value) {
    switch (value) {
        case UploadStatus::Pending: return "pending";
        case UploadStatus::Receiving: return "receiving";
        case UploadStatus::Complete: return "complete";
        case UploadStatus::Failed: return "failed";
        case UploadStatus::Cancelled: return "cancelled";
    }
    throw std::invalid_argument("Invalid share upload status");
}

std::string to_string(const AuditActorType value) {
    switch (value) {
        case AuditActorType::OwnerUser: return "owner_user";
        case AuditActorType::AdminUser: return "admin_user";
        case AuditActorType::SharePrincipal: return "share_principal";
        case AuditActorType::System: return "system";
        case AuditActorType::Unknown: return "unknown";
    }
    throw std::invalid_argument("Invalid share audit actor type");
}

std::string to_string(const AuditStatus value) {
    switch (value) {
        case AuditStatus::Success: return "success";
        case AuditStatus::Denied: return "denied";
        case AuditStatus::Failed: return "failed";
        case AuditStatus::RateLimited: return "rate_limited";
    }
    throw std::invalid_argument("Invalid share audit status");
}

Operation operation_from_string(const std::string_view value) {
    if (value == "metadata") return Operation::Metadata;
    if (value == "list") return Operation::List;
    if (value == "preview") return Operation::Preview;
    if (value == "download") return Operation::Download;
    if (value == "upload") return Operation::Upload;
    if (value == "mkdir") return Operation::Mkdir;
    if (value == "overwrite") return Operation::Overwrite;
    return fail_enum<Operation>(value);
}

TargetType target_type_from_string(const std::string_view value) {
    if (value == "file") return TargetType::File;
    if (value == "directory") return TargetType::Directory;
    return fail_enum<TargetType>(value);
}

LinkType link_type_from_string(const std::string_view value) {
    if (value == "download") return LinkType::Download;
    if (value == "upload") return LinkType::Upload;
    if (value == "access") return LinkType::Access;
    return fail_enum<LinkType>(value);
}

AccessMode access_mode_from_string(const std::string_view value) {
    if (value == "public") return AccessMode::Public;
    if (value == "email_validated") return AccessMode::EmailValidated;
    return fail_enum<AccessMode>(value);
}

DuplicatePolicy duplicate_policy_from_string(const std::string_view value) {
    if (value == "reject") return DuplicatePolicy::Reject;
    if (value == "rename") return DuplicatePolicy::Rename;
    if (value == "overwrite") return DuplicatePolicy::Overwrite;
    return fail_enum<DuplicatePolicy>(value);
}

UploadStatus upload_status_from_string(const std::string_view value) {
    if (value == "pending") return UploadStatus::Pending;
    if (value == "receiving") return UploadStatus::Receiving;
    if (value == "complete") return UploadStatus::Complete;
    if (value == "failed") return UploadStatus::Failed;
    if (value == "cancelled") return UploadStatus::Cancelled;
    return fail_enum<UploadStatus>(value);
}

AuditActorType audit_actor_type_from_string(const std::string_view value) {
    if (value == "owner_user") return AuditActorType::OwnerUser;
    if (value == "admin_user") return AuditActorType::AdminUser;
    if (value == "share_principal") return AuditActorType::SharePrincipal;
    if (value == "system") return AuditActorType::System;
    if (value == "unknown") return AuditActorType::Unknown;
    return fail_enum<AuditActorType>(value);
}

AuditStatus audit_status_from_string(const std::string_view value) {
    if (value == "success") return AuditStatus::Success;
    if (value == "denied") return AuditStatus::Denied;
    if (value == "failed") return AuditStatus::Failed;
    if (value == "rate_limited") return AuditStatus::RateLimited;
    return fail_enum<AuditStatus>(value);
}

void to_json(nlohmann::json& j, const Operation value) { j = to_string(value); }
void to_json(nlohmann::json& j, const TargetType value) { j = to_string(value); }
void to_json(nlohmann::json& j, const LinkType value) { j = to_string(value); }
void to_json(nlohmann::json& j, const AccessMode value) { j = to_string(value); }
void to_json(nlohmann::json& j, const DuplicatePolicy value) { j = to_string(value); }
void to_json(nlohmann::json& j, const UploadStatus value) { j = to_string(value); }
void to_json(nlohmann::json& j, const AuditActorType value) { j = to_string(value); }
void to_json(nlohmann::json& j, const AuditStatus value) { j = to_string(value); }

void from_json(const nlohmann::json& j, Operation& value) { value = operation_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, TargetType& value) { value = target_type_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, LinkType& value) { value = link_type_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, AccessMode& value) { value = access_mode_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, DuplicatePolicy& value) { value = duplicate_policy_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, UploadStatus& value) { value = upload_status_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, AuditActorType& value) { value = audit_actor_type_from_string(j.get<std::string>()); }
void from_json(const nlohmann::json& j, AuditStatus& value) { value = audit_status_from_string(j.get<std::string>()); }

}
