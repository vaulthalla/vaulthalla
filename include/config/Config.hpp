#pragma once

#include "log/Rotator.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <nlohmann/json_fwd.hpp>
#include <chrono>

namespace vh::config {

constexpr static uintmax_t MAX_UPLOAD_SIZE_BYTES = static_cast<uintmax_t>(2) * 1024 * 1024 * 1024; // 2GB
constexpr static uintmax_t MAX_PREVIEW_SIZE_BYTES = 100 * 1024 * 1024;        // 100MB

struct WebsocketConfig {
    bool enabled = true;
    std::string host = "0.0.0.0";
    uint16_t port = 33369;
    unsigned int max_connections = 1024;
    uintmax_t max_upload_size_bytes = MAX_UPLOAD_SIZE_BYTES;
};

struct HttpPreviewConfig {
    bool enabled = true;
    std::string host = "0.0.0.0";
    uint16_t port = 33370;
    unsigned int max_connections = 512;
    uintmax_t max_preview_size_bytes = MAX_PREVIEW_SIZE_BYTES;
};

struct ThumbnailsConfig {
    std::vector<std::string> formats = {"jpg", "jpeg", "png", "webp", "pdf"};
    std::vector<unsigned int> sizes = {128, 256, 512};
    unsigned int expiry_days = 30;
};

struct CachingConfig {
    unsigned int max_size_mb = 10240;
    ThumbnailsConfig thumbnails;
};

struct DatabaseConfig {
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string name = "vaulthalla";
    std::string user = "vaulthalla";
    int pool_size = 10;
};

struct AuthConfig {
    unsigned int access_token_expiry_minutes = 60;
    unsigned int refresh_token_expiry_days = 7;
};

struct SyncConfig {
    uint32_t event_audit_retention_days = 30;
    uint32_t event_audit_max_entries = 10000;
};

struct DBSweeperConfig {
    uint32_t sweep_interval_minutes = 60;
};

struct ConnectionLifecycleManagerConfig {
    uint32_t idle_timeout_minutes = 30;
    uint32_t unauthenticated_timeout_seconds = 60;
    uint32_t sweep_interval_seconds = 60;
};

struct ServicesConfig {
    DBSweeperConfig db_sweeper;
    ConnectionLifecycleManagerConfig connection_lifecycle_manager;
};

struct SharingConfig {
    bool enabled = true;
    bool enable_public_links = true;
};

struct AuditLogConfig {
    std::chrono::days retention_days = std::chrono::days(30);
    uintmax_t rotate_max_size = 50 * 1024 * 1024; // 50MB
    std::chrono::hours rotate_interval = std::chrono::hours(24);
    log::Rotator::Compression compression = log::Rotator::Compression::Zstd;
    uintmax_t max_retained_logs_size = 1024 * 1024 * 1024; // 1GB
    bool strict_retention = false; // If true, retain logs for full retention days minimum, even if over the size limit
};

struct EncryptionWaiverConfig {
    std::chrono::days retention_days = std::chrono::days(180);
};

struct FilesTrashedConfig {
    std::chrono::days retention_days = std::chrono::days(60);
};

struct AuditConfig {
    AuditLogConfig audit_log;
    EncryptionWaiverConfig encryption_waivers;
    FilesTrashedConfig files_trashed;
};

struct DevConfig {
    bool enabled = false;
    bool init_r2_test_vault = false;
};

struct SubsystemLogLevelsConfig {
    spdlog::level::level_enum vaulthalla   = spdlog::level::info;   // Top-level events like startup/shutdown
    spdlog::level::level_enum fuse         = spdlog::level::warn;   // Don’t log every op; only surface permission or IO failures
    spdlog::level::level_enum filesystem   = spdlog::level::warn;   // Only structural errors or corruption
    spdlog::level::level_enum crypto       = spdlog::level::warn;   // Rare; surface failure to encrypt/decrypt
    spdlog::level::level_enum cloud        = spdlog::level::warn;   // AWS/S3 errors, not routine syncs
    spdlog::level::level_enum auth         = spdlog::level::warn;   // Failed logins, token errors
    spdlog::level::level_enum websocket    = spdlog::level::warn;   // Auth failures, closed sockets, hijack attempts
    spdlog::level::level_enum http         = spdlog::level::warn;   // 5xx, invalid auth, etc.
    spdlog::level::level_enum shell        = spdlog::level::warn;   // CLI parsing edge cases or override violations
    spdlog::level::level_enum db           = spdlog::level::err;    // Only if DB is unreachable, failed tx, corruption
    spdlog::level::level_enum sync         = spdlog::level::warn;   // Conflict resolution issues, failed upload/download
    spdlog::level::level_enum thumb        = spdlog::level::warn;   // Failed renders only
    spdlog::level::level_enum storage      = spdlog::level::warn;   // Underlying I/O issues
    spdlog::level::level_enum types        = spdlog::level::err;    // Violations of invariants or schema errors
};

struct LogLevelsConfig {
    spdlog::level::level_enum console_log_level = spdlog::level::info;
    spdlog::level::level_enum file_log_level = spdlog::level::warn;
    SubsystemLogLevelsConfig subsystem_levels;
};

struct LoggingConfig {
    LogLevelsConfig levels;
};

struct Config {
    WebsocketConfig websocket;
    HttpPreviewConfig http_preview;
    CachingConfig caching;
    DatabaseConfig database;
    AuthConfig auth;
    SyncConfig sync;
    ServicesConfig services;
    SharingConfig sharing;
    AuditConfig auditing;
    DevConfig dev;

    LoggingConfig logging; // internal only

    void save() const;
};

Config loadConfig(const std::string& path);
void to_json(nlohmann::json& j, const Config& c);
void from_json(const nlohmann::json& j, Config& c);
void to_json(nlohmann::json& j, const WebsocketConfig& c);
void from_json(const nlohmann::json& j, WebsocketConfig& c);
void to_json(nlohmann::json& j, const HttpPreviewConfig& c);
void from_json(const nlohmann::json& j, HttpPreviewConfig& c);
void to_json(nlohmann::json& j, const LogLevelsConfig& c);
void from_json(const nlohmann::json& j, LogLevelsConfig& c);
void to_json(nlohmann::json& j, const SubsystemLogLevelsConfig& c);
void from_json(const nlohmann::json& j, SubsystemLogLevelsConfig& c);
void to_json(nlohmann::json& j, const LoggingConfig& c);
void from_json(const nlohmann::json& j, LoggingConfig& c);
void to_json(nlohmann::json& j, const ThumbnailsConfig& c);
void from_json(const nlohmann::json& j, ThumbnailsConfig& c);
void to_json(nlohmann::json& j, const CachingConfig& c);
void from_json(const nlohmann::json& j, CachingConfig& c);
void to_json(nlohmann::json& j, const DatabaseConfig& c);
void from_json(const nlohmann::json& j, DatabaseConfig& c);
void to_json(nlohmann::json& j, const AuthConfig& c);
void from_json(const nlohmann::json& j, AuthConfig& c);
void to_json(nlohmann::json& j, const SyncConfig& c);
void from_json(const nlohmann::json& j, SyncConfig& c);
void to_json(nlohmann::json& j, const DBSweeperConfig& c);
void from_json(const nlohmann::json& j, DBSweeperConfig& c);
void to_json(nlohmann::json& j, const ConnectionLifecycleManagerConfig& c);
void from_json(const nlohmann::json& j, ConnectionLifecycleManagerConfig& c);
void to_json(nlohmann::json& j, const ServicesConfig& c);
void from_json(const nlohmann::json& j, ServicesConfig& c);
void to_json(nlohmann::json& j, const SharingConfig& c);
void from_json(const nlohmann::json& j, SharingConfig& c);
void to_json(nlohmann::json& j, const AuditLogConfig& c);
void from_json(const nlohmann::json& j, AuditLogConfig& c);
void to_json(nlohmann::json& j, const EncryptionWaiverConfig& c);
void from_json(const nlohmann::json& j, EncryptionWaiverConfig& c);
void to_json(nlohmann::json& j, const FilesTrashedConfig& c);
void from_json(const nlohmann::json& j, FilesTrashedConfig& c);
void to_json(nlohmann::json& j, const AuditConfig& c);
void from_json(const nlohmann::json& j, AuditConfig& c);
void to_json(nlohmann::json& j, const DevConfig& c);
void from_json(const nlohmann::json& j, DevConfig& c);

} // namespace vh::config
