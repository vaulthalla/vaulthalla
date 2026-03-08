#pragma once

#include "config/Config.hpp"
#include "config/util.hpp"

#include <yaml-cpp/yaml.h>

namespace YAML {

using namespace vh::config;

template<>
struct convert<WebsocketConfig> {
    static Node encode(const WebsocketConfig& rhs) {
        Node node;
        node["enabled"] = rhs.enabled;
        node["host"] = rhs.host;
        node["port"] = rhs.port;
        node["max_connections"] = rhs.max_connections;
        node["max_upload_size_bytes"] = rhs.max_upload_size_bytes;
        return node;
    }

    static bool decode(const Node& node, WebsocketConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.enabled = node["enabled"].as<bool>(true);
        rhs.host = node["host"].as<std::string>("0.0.0.0");
        rhs.port = node["port"].as<uint16_t>(8080);
        rhs.max_connections = node["max_connections"].as<unsigned int>(1024);
        rhs.max_upload_size_bytes = node["max_upload_size_mb"].as<uintmax_t>(2048) * 1024 * 1024; // Default 2GB
        return true;
    }
};

template<>
struct convert<HttpPreviewConfig> {
    static Node encode(const HttpPreviewConfig& rhs) {
        Node node;
        node["enabled"] = rhs.enabled;
        node["host"] = rhs.host;
        node["port"] = rhs.port;
        node["max_connections"] = rhs.max_connections;
        node["max_preview_size_bytes"] = rhs.max_preview_size_bytes;
        return node;
    }

    static bool decode(const Node& node, HttpPreviewConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.enabled = node["enabled"].as<bool>(true);
        rhs.host = node["host"].as<std::string>("0.0.0.0");
        rhs.port = node["port"].as<uint16_t>(8081);
        rhs.max_connections = node["max_connections"].as<unsigned int>(512);
        rhs.max_preview_size_bytes = node["max_preview_size_mb"].as<uintmax_t>(100) * 1024 * 1024; // Default 100MB
        return true;
    }
};

static std::string to_std_string(const spdlog::string_view_t sv) { return {sv.data(), sv.size()}; }

template<>
struct convert<SubsystemLogLevelsConfig> {
    static Node encode(const SubsystemLogLevelsConfig& rhs) {
        Node node;
        node["vaulthalla"]  = to_std_string(spdlog::level::to_string_view(rhs.vaulthalla));
        node["fuse"]        = to_std_string(spdlog::level::to_string_view(rhs.fuse));
        node["filesystem"]  = to_std_string(spdlog::level::to_string_view(rhs.filesystem));
        node["crypto"]      = to_std_string(spdlog::level::to_string_view(rhs.crypto));
        node["cloud"]       = to_std_string(spdlog::level::to_string_view(rhs.cloud));
        node["auth"]        = to_std_string(spdlog::level::to_string_view(rhs.auth));
        node["websocket"]   = to_std_string(spdlog::level::to_string_view(rhs.websocket));
        node["http"]        = to_std_string(spdlog::level::to_string_view(rhs.http));
        node["shell"]       = to_std_string(spdlog::level::to_string_view(rhs.shell));
        node["db"]          = to_std_string(spdlog::level::to_string_view(rhs.db));
        node["sync"]        = to_std_string(spdlog::level::to_string_view(rhs.sync));
        node["thumb"]       = to_std_string(spdlog::level::to_string_view(rhs.thumb));
        node["storage"]     = to_std_string(spdlog::level::to_string_view(rhs.storage));
        node["types"]       = to_std_string(spdlog::level::to_string_view(rhs.types));
        return node;
    }

    static bool decode(const Node& node, SubsystemLogLevelsConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.vaulthalla = spdlog::level::from_str(node["vaulthalla"].as<std::string>("debug"));
        rhs.fuse = spdlog::level::from_str(node["fuse"].as<std::string>("debug"));
        rhs.filesystem = spdlog::level::from_str(node["filesystem"].as<std::string>("info"));
        rhs.crypto = spdlog::level::from_str(node["crypto"].as<std::string>("info"));
        rhs.cloud = spdlog::level::from_str(node["cloud"].as<std::string>("info"));
        rhs.auth = spdlog::level::from_str(node["auth"].as<std::string>("info"));
        rhs.websocket = spdlog::level::from_str(node["websocket"].as<std::string>("info"));
        rhs.http = spdlog::level::from_str(node["http"].as<std::string>("info"));
        rhs.shell = spdlog::level::from_str(node["shell"].as<std::string>("info"));
        rhs.db = spdlog::level::from_str(node["db"].as<std::string>("warn"));
        rhs.sync = spdlog::level::from_str(node["sync"].as<std::string>("info"));
        rhs.thumb = spdlog::level::from_str(node["thumb"].as<std::string>("info"));
        rhs.storage = spdlog::level::from_str(node["storage"].as<std::string>("info"));
        rhs.types = spdlog::level::from_str(node["types"].as<std::string>("info"));
        return true;
    }
};

template<>
struct convert<LogLevelsConfig> {
    static Node encode(const LogLevelsConfig& rhs) {
        Node node;
        node["console_log_level"] = to_std_string(spdlog::level::to_string_view(rhs.console_log_level));
        node["file_log_level"]    = to_std_string(spdlog::level::to_string_view(rhs.file_log_level));
        node["subsystem_levels"]  = rhs.subsystem_levels;
        return node;
    }

    static bool decode(const Node& node, LogLevelsConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.console_log_level = spdlog::level::from_str(node["console_log_level"].as<std::string>("info"));
        rhs.file_log_level = spdlog::level::from_str(node["file_log_level"].as<std::string>("debug"));
        rhs.subsystem_levels = node["subsystem_levels"].as<SubsystemLogLevelsConfig>();
        return true;
    }
};

// LoggingConfig
template<>
struct convert<LoggingConfig> {
    static Node encode(const LoggingConfig& rhs) {
        Node node;
        node["log_levels"] = rhs.levels;
        return node;
    }

    static bool decode(const Node& node, LoggingConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.levels = node["log_levels"].as<LogLevelsConfig>();
        return true;
    }
};

template<>
struct convert<ThumbnailsConfig> {
    static Node encode(const ThumbnailsConfig& rhs) {
        Node node;
        node["formats"] = rhs.formats;
        node["sizes"] = rhs.sizes;
        node["expiry_days"] = rhs.expiry_days;
        return node;
    }

    static bool decode(const Node& node, ThumbnailsConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.formats = node["formats"].as<std::vector<std::string>>();
        rhs.sizes = node["sizes"].as<std::vector<unsigned int>>();
        rhs.expiry_days = node["expiry_days"].as<unsigned int>(30);
        return true;
    }
};

template<>
struct convert<CachingConfig> {
    static Node encode(const CachingConfig& rhs) {
        Node node;
        node["max_size_mb"] = rhs.max_size_mb;
        node["thumbnails"] = rhs.thumbnails;
        return node;
    }

    static bool decode(const Node& node, CachingConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.max_size_mb = node["max_size_mb"].as<unsigned int>(10240);
        rhs.thumbnails = node["thumbnails"].as<ThumbnailsConfig>();
        return true;
    }
};

template<>
struct convert<DatabaseConfig> {
    static Node encode(const DatabaseConfig& rhs) {
        Node node;
        node["host"] = rhs.host;
        node["port"] = rhs.port;
        node["name"] = rhs.name;
        node["user"] = rhs.user;
        node["pool_size"] = rhs.pool_size;
        return node;
    }

    static bool decode(const Node& node, DatabaseConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.host = node["host"].as<std::string>("localhost");
        rhs.port = node["port"].as<uint16_t>(5432);
        rhs.name = node["name"].as<std::string>("vaulthalla");
        rhs.user = node["user"].as<std::string>("vaulthalla");
        rhs.pool_size = node["pool_size"].as<int>(10);
        return true;
    }
};

// AuthConfig

template<>
struct convert<AuthConfig> {
    static Node encode(const AuthConfig& rhs) {
        Node node;
        node["token_expiry_minutes"] = rhs.access_token_expiry_minutes;
        node["refresh_token_expiry_days"] = rhs.refresh_token_expiry_days;
        return node;
    }

    static bool decode(const Node& node, AuthConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.access_token_expiry_minutes = node["token_expiry_minutes"].as<int>(60);
        rhs.refresh_token_expiry_days = node["refresh_token_expiry_days"].as<int>(7);
        return true;
    }
};

// SyncConfig
template<>
struct convert<SyncConfig> {
    static Node encode(const SyncConfig& rhs) {
        Node node;
        node["event_audit_retention_days"] = rhs.event_audit_retention_days;
        node["event_audit_max_entries"] = rhs.event_audit_max_entries;
        return node;
    }

    static bool decode(const Node& node, SyncConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.event_audit_retention_days = std::max(static_cast<uint32_t>(7), node["event_audit_retention_days"].as<uint32_t>(30));
        rhs.event_audit_max_entries = std::max(static_cast<uint32_t>(1000), node["event_audit_max_entries"].as<uint32_t>(10000));
        return true;
    }
};

template<>
struct convert<DBSweeperConfig> {
    static Node encode(const DBSweeperConfig& rhs) {
        Node node;
        node["sweep_interval_minutes"] = rhs.sweep_interval_minutes;
        return node;
    }

    static bool decode(const Node& node, DBSweeperConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.sweep_interval_minutes = std::max(10, node["sweep_interval_minutes"].as<int>(60));
        return true;
    }
};

template<>
struct convert<ConnectionLifecycleManagerConfig> {
    static Node encode(const ConnectionLifecycleManagerConfig& rhs) {
        Node node;
        node["idle_timeout_minutes"] = rhs.idle_timeout_minutes;
        node["unauthenticated_timeout_seconds"] = rhs.unauthenticated_timeout_seconds;
        node["sweep_interval_seconds"] = rhs.sweep_interval_seconds;
        return node;
    }

    static bool decode(const Node& node, ConnectionLifecycleManagerConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.idle_timeout_minutes = std::max(5, node["idle_timeout_minutes"].as<int>(30));
        rhs.unauthenticated_timeout_seconds = std::max(30, node["unauthenticated_timeout_seconds"].as<int>(300));
        rhs.sweep_interval_seconds = std::max(15, node["sweep_interval_seconds"].as<int>(60));
        return true;
    }
};

template<>
struct convert<ServicesConfig> {
    static Node encode(const ServicesConfig& rhs) {
        Node node;
        node["db_sweeper"] = rhs.db_sweeper;
        node["connection_lifecycle_manager"] = rhs.connection_lifecycle_manager;
        return node;
    }

    static bool decode(const Node& node, ServicesConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.db_sweeper = node["db_sweeper"].as<DBSweeperConfig>();
        rhs.connection_lifecycle_manager = node["connection_lifecycle_manager"].as<ConnectionLifecycleManagerConfig>();
        return true;
    }
};

template<>
struct convert<SharingConfig> {
    static Node encode(const SharingConfig& rhs) {
        Node node;
        node["enabled"] = rhs.enabled;
        node["enable_public_links"] = rhs.enable_public_links;
        return node;
    }

    static bool decode(const Node& node, SharingConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.enabled = node["enabled"].as<bool>(true);
        rhs.enable_public_links = node["enable_public_links"].as<bool>(true);
        return true;
    }
};

template<>
struct convert<AuditLogConfig> {
    static Node encode(const AuditLogConfig& rhs) {
        Node node;
        node["retention_days"] = rhs.retention_days.count();
        node["rotate_max_size"] = bytesToMbOrGbStr(rhs.rotate_max_size);
        node["rotate_interval"] = hoursToDayOrHourStr(rhs.rotate_interval);
        node["compression"] = compressionToString(rhs.compression);
        node["max_retained_logs_size"] = bytesToMbOrGbStr(rhs.max_retained_logs_size);
        node["strict_retention"] = rhs.strict_retention;
        return node;
    }

    static bool decode(const Node& node, AuditLogConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.retention_days = std::chrono::days(node["retention_days"].as<unsigned int>(30));
        rhs.rotate_max_size = parseMbOrGbToByte(node["rotate_max_size"].as<std::string>("50MB"));
        rhs.rotate_interval = parseHoursFromDayOrHour(node["rotate_interval"].as<std::string>("24h"));
        rhs.compression = parseCompression(node["compression"].as<std::string>("zstd"));
        rhs.max_retained_logs_size = parseMbOrGbToByte(node["max_retained_logs_size"].as<std::string>("1GB"));
        rhs.strict_retention = node["strict_retention"].as<bool>(false);
        return true;
    }
};

template<>
struct convert<EncryptionWaiverConfig> {
    static Node encode(const EncryptionWaiverConfig& rhs) {
        Node node;
        node["retention_days"] = rhs.retention_days.count();
        return node;
    }

    static bool decode(const Node& node, EncryptionWaiverConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.retention_days = std::chrono::days(node["retention_days"].as<unsigned int>(180));
        return true;
    }
};

template<>
struct convert<FilesTrashedConfig> {
    static Node encode(const FilesTrashedConfig& rhs) {
        Node node;
        node["retention_days"] = rhs.retention_days.count();
        return node;
    }

    static bool decode(const Node& node, FilesTrashedConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.retention_days = std::chrono::days(node["retention_days"].as<unsigned int>(60));
        return true;
    }
};

template<>
struct convert<AuditConfig> {
    static Node encode(const AuditConfig& rhs) {
        Node node;
        node["audit_log"] = rhs.audit_log;
        node["encryption_waivers"] = rhs.encryption_waivers;
        node["files_trashed"] = rhs.files_trashed;
        return node;
    }

    static bool decode(const Node& node, AuditConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.audit_log = node["audit_log"].as<AuditLogConfig>();
        rhs.encryption_waivers = node["encryption_waivers"].as<EncryptionWaiverConfig>();
        rhs.files_trashed = node["files_trashed"].as<FilesTrashedConfig>();
        return true;
    }
};

template<>
struct convert<DevConfig> {
    static Node encode(const DevConfig& rhs) {
        Node node;
        node["enabled"] = rhs.enabled;
        node["init_r2_test_vault"] = rhs.init_r2_test_vault;
        return node;
    }

    static bool decode(const Node& node, DevConfig& rhs) {
        if (!node.IsMap()) return false;
        rhs.enabled = node["enabled"].as<bool>(false);
        rhs.init_r2_test_vault = node["init_r2_test_vault"].as<bool>(false);
        return true;
    }
};

template<>
struct convert<std::filesystem::path> {
    static Node encode(const std::filesystem::path& rhs) {
        return Node(rhs.string());
    }

    static bool decode(const Node& node, std::filesystem::path& rhs) {
        if (!node.IsScalar()) return false;
        rhs = std::filesystem::path(node.as<std::string>());
        return true;
    }
};

} // namespace YAML
