#include "config/Config.hpp"
#include "config/config_yaml.hpp"
#include "config/util.hpp"

#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <paths.h>
#include <nlohmann/json.hpp>

namespace vh::config {
    template<typename T>
    T getOrDefault(const YAML::Node &node, const std::string &key, const T &def) {
        return node[key] ? node[key].as<T>() : def;
    }

    std::string emailProviderKindToString(const EmailProviderKind kind) {
        switch (kind) {
            case EmailProviderKind::None: return "none";
            case EmailProviderKind::Resend: return "resend";
            case EmailProviderKind::Ses: return "ses";
        }
        return "none";
    }

    EmailProviderKind emailProviderKindFromString(std::string_view value) {
        std::string normalized(value);
        std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (normalized.empty() || normalized == "none") return EmailProviderKind::None;
        if (normalized == "resend") return EmailProviderKind::Resend;
        if (normalized == "ses") return EmailProviderKind::Ses;
        throw std::invalid_argument("unknown email provider: " + normalized);
    }

    Config loadConfig(const std::string &path) {
        Config cfg;
        YAML::Node root = YAML::LoadFile(path);

        if (auto node = root["websocket_server"]) YAML::convert<WebsocketConfig>::decode(node, cfg.websocket);
        if (auto node = root["http_preview_server"]) YAML::convert<HttpPreviewConfig>::decode(node, cfg.http_preview);
        if (auto node = root["caching"]) YAML::convert<CachingConfig>::decode(node, cfg.caching);
        if (auto node = root["database"]) YAML::convert<DatabaseConfig>::decode(node, cfg.database);
        if (auto node = root["auth"]) YAML::convert<AuthConfig>::decode(node, cfg.auth);
        if (auto node = root["sync"]) YAML::convert<SyncConfig>::decode(node, cfg.sync);
        if (auto node = root["services"]) YAML::convert<ServicesConfig>::decode(node, cfg.services);
        if (auto node = root["stats_snapshots"]) YAML::convert<StatsSnapshotsConfig>::decode(node, cfg.stats_snapshots);
        if (auto node = root["sharing"]) YAML::convert<SharingConfig>::decode(node, cfg.sharing);
        if (auto node = root["email"]) YAML::convert<EmailConfig>::decode(node, cfg.email);
        if (auto node = root["operator_emails"]) YAML::convert<OperatorEmailsConfig>::decode(node, cfg.operator_emails);
        if (auto node = root["auditing"]) YAML::convert<AuditConfig>::decode(node, cfg.auditing);
        if (auto node = root["dev"]) YAML::convert<DevConfig>::decode(node, cfg.dev);

        if (auto node = root["logging"]) YAML::convert<LoggingConfig>::decode(node, cfg.logging);

        return cfg;
    }

    void Config::save() const {
        using namespace std;
        namespace fs = std::filesystem;

        const fs::path configFile = paths::getConfigPath();
        const fs::path templateFile = paths::getConfigPath().parent_path() / "config_template.yaml.in";

        YAML::Node root;
        if (fs::exists(configFile)) root = YAML::LoadFile(configFile.string());
        else if (fs::exists(templateFile)) root = YAML::LoadFile(templateFile.string());
        else root = YAML::Node(YAML::NodeType::Map);

        if (!root || !root.IsMap()) root = YAML::Node(YAML::NodeType::Map);

        const auto put = [&root]<typename T>(const std::string& key, const T& section) {
            root[key] = YAML::convert<T>::encode(section);
        };

        put("websocket_server", websocket);
        put("http_preview_server", http_preview);
        put("database", database);
        put("auth", auth);
        put("sync", sync);
        put("services", services);
        put("stats_snapshots", stats_snapshots);
        put("sharing", sharing);
        put("email", email);
        put("operator_emails", operator_emails);
        put("caching", caching);
        put("auditing", auditing);
        put("logging", logging);
        put("dev", dev);

        // Write the final result
        ofstream out(configFile);
        if (!out.is_open()) {
            throw runtime_error("Failed to write config file");
        }

        YAML::Emitter emitted;
        emitted << root;
        out << emitted.c_str() << '\n';
        out.close();
    }

    void to_json(nlohmann::json &j, const Config &c) {
        j = {
            {"websocket_server", c.websocket},
            {"http_preview_server", c.http_preview},
            {"caching", c.caching},
            {"database", c.database},
            {"auth", c.auth},
            {"sync", c.sync},
            {"services", c.services},
            {"stats_snapshots", c.stats_snapshots},
            {"sharing", c.sharing},
            {"email", c.email},
            {"operator_emails", c.operator_emails},
            {"auditing", c.auditing},
            {"logging", c.logging},
            {"dev", c.dev}
        };
    }

    void from_json(const nlohmann::json &j, Config &c) {
        j.at("websocket_server").get_to(c.websocket);
        j.at("http_preview_server").get_to(c.http_preview);
        j.at("caching").get_to(c.caching);
        j.at("database").get_to(c.database);
        j.at("auth").get_to(c.auth);
        j.at("sync").get_to(c.sync);
        j.at("services").get_to(c.services);
        if (j.contains("stats_snapshots")) j.at("stats_snapshots").get_to(c.stats_snapshots);
        j.at("sharing").get_to(c.sharing);
        if (j.contains("email")) j.at("email").get_to(c.email);
        if (j.contains("operator_emails")) j.at("operator_emails").get_to(c.operator_emails);
        j.at("auditing").get_to(c.auditing);
        if (j.contains("logging")) j.at("logging").get_to(c.logging);
        j.at("dev").get_to(c.dev);
    }

    void to_json(nlohmann::json &j, const WebsocketConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"host", c.host},
            {"port", c.port},
            {"max_connections", c.max_connections},
            {"max_upload_size_bytes", c.max_upload_size_bytes}
        };
    }

    void from_json(const nlohmann::json &j, WebsocketConfig &c) {
        c.enabled = j.value("enabled", true);
        c.host = j.value("host", "0.0.0.0");
        c.port = j.value("port", 33369);
        c.max_connections = j.value("max_connections", 10000);
        c.max_upload_size_bytes = j.value("max_upload_size_bytes", MAX_UPLOAD_SIZE_BYTES);
    }

    void to_json(nlohmann::json &j, const HttpPreviewConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"host", c.host},
            {"port", c.port}
        };
    }

    void from_json(const nlohmann::json &j, HttpPreviewConfig &c) {
        c.enabled = j.value("enabled", true);
        c.host = j.value("host", "0.0.0.0");
        c.port = j.value("port", 33370);
        c.max_connections = j.value("max_connections", 512);
        c.max_preview_size_bytes = j.value("max_preview_size_bytes", MAX_PREVIEW_SIZE_BYTES);
    }

    void to_json(nlohmann::json &j, const LoggingConfig &c) {
        j = {
            {"levels", c.levels}
        };
    }

    void from_json(const nlohmann::json &j, LoggingConfig &c) {
        j.at("levels").get_to(c.levels);
    }

    void to_json(nlohmann::json &j, const LogLevelsConfig &c) {
        j = {
            {"console_log_level", c.console_log_level},
            {"file_log_level", c.file_log_level},
            {"subsystem_levels", c.subsystem_levels}
        };
    }

    void from_json(const nlohmann::json &j, LogLevelsConfig &c) {
        c.console_log_level = j.value("console_log_level", spdlog::level::info);
        c.file_log_level = j.value("file_log_level", spdlog::level::debug);
        j.at("subsystem_levels").get_to(c.subsystem_levels);
    }

    void to_json(nlohmann::json &j, const SubsystemLogLevelsConfig &c) {
        j = {
            {"vaulthalla", c.vaulthalla},
            {"filesystem", c.filesystem},
            {"crypto", c.crypto},
            {"cloud", c.cloud},
            {"auth", c.auth},
            {"websocket", c.websocket},
            {"http", c.http},
            {"shell", c.shell},
            {"db", c.db},
            {"sync", c.sync},
            {"thumb", c.thumb},
            {"storage", c.storage},
            {"types", c.types},
            {"runtime", c.runtime}
        };
    }

    void from_json(const nlohmann::json &j, SubsystemLogLevelsConfig &c) {
        c.vaulthalla = j.value("vaulthalla", spdlog::level::debug);
        c.filesystem = j.value("filesystem", spdlog::level::info);
        c.crypto = j.value("crypto", spdlog::level::info);
        c.cloud = j.value("cloud", spdlog::level::info);
        c.auth = j.value("auth", spdlog::level::info);
        c.websocket = j.value("websocket", spdlog::level::info);
        c.http = j.value("http", spdlog::level::info);
        c.shell = j.value("shell", spdlog::level::info);
        c.db = j.value("db", spdlog::level::warn);
        c.sync = j.value("sync", spdlog::level::info);
        c.thumb = j.value("thumb", spdlog::level::info);
        c.storage = j.value("storage", spdlog::level::info);
        c.types = j.value("types", spdlog::level::info);
        c.runtime = j.value("runtime", spdlog::level::info);
    }

    void to_json(nlohmann::json &j, const ThumbnailsConfig &c) {
        j = {
            {"formats", c.formats},
            {"sizes", c.sizes},
            {"expiry_days", c.expiry_days}
        };
    }

    void from_json(const nlohmann::json &j, ThumbnailsConfig &c) {
        c.formats = j.value("formats", std::vector<std::string>{"jpg", "jpeg", "png", "webp", "pdf"});
        c.sizes = j.value("sizes", std::vector<unsigned int>{128, 256, 512});
        c.expiry_days = j.value("expiry_days", 30);
    }

    void to_json(nlohmann::json &j, const CachingConfig &c) {
        j = {
            {"thumbnails", c.thumbnails},
            {"max_size_mb", c.max_size_mb}
        };
    }

    void from_json(const nlohmann::json &j, CachingConfig &c) {
        j.at("thumbnails").get_to(c.thumbnails);
        c.max_size_mb = j.value("max_size_mb", 10240);
    }

    void to_json(nlohmann::json &j, const DatabaseConfig &c) {
        j = {
            {"host", c.host},
            {"port", c.port},
            {"name", c.name},
            {"user", c.user},
            {"pool_size", c.pool_size}
        };
    }

    void from_json(const nlohmann::json &j, DatabaseConfig &c) {
        c.host = j.value("host", "localhost");
        c.port = j.value("port", 5432);
        c.name = j.value("name", "vaulthalla");
        c.user = j.value("user", "vaulthalla");
        c.pool_size = j.value("pool_size", 10);
    }

    void to_json(nlohmann::json &j, const AuthConfig &c) {
        j = {
            {"access_token_expiry_minutes", c.access_token_expiry_minutes},
            {"refresh_token_expiry_days", c.refresh_token_expiry_days}
            // Do not serialize jwt_secret
        };
    }

    void from_json(const nlohmann::json &j, AuthConfig &c) {
        c.access_token_expiry_minutes = j.value("access_token_expiry_minutes", 60);
        c.refresh_token_expiry_days = j.value("refresh_token_expiry_days", 7);
    }

    void to_json(nlohmann::json &j, const SyncConfig &c) {
        j = {
            {"event_audit_retention_days", c.event_audit_retention_days},
            {"event_audit_max_entries", c.event_audit_max_entries}
        };
    }

    void from_json(const nlohmann::json &j, SyncConfig &c) {
        c.event_audit_retention_days = std::max(7, j.value("event_audit_retention_days", 30));
        c.event_audit_max_entries = std::max(1000, j.value("event_audit_max_entries", 10000));
    }

    void to_json(nlohmann::json &j, const DBSweeperConfig &c) {
        j = {
            {"sweep_interval_minutes", c.sweep_interval_minutes}
        };
    }

    void from_json(const nlohmann::json &j, DBSweeperConfig &c) {
        c.sweep_interval_minutes = std::max(5, j.value("sweep_interval_minutes", 60));
    }

    void to_json(nlohmann::json &j, const ConnectionLifecycleManagerConfig &c) {
        j = {
            {"idle_timeout_minutes", c.idle_timeout_minutes},
            {"unauthenticated_timeout_seconds", c.unauthenticated_timeout_seconds},
            {"sweep_interval_seconds", c.sweep_interval_seconds}
        };
    }

    void from_json(const nlohmann::json &j, ConnectionLifecycleManagerConfig &c) {
        c.idle_timeout_minutes = std::max(5, j.value("idle_timeout_minutes", 30));
        c.unauthenticated_timeout_seconds = std::max(30, j.value("unauthenticated_timeout_seconds", 300));
        c.sweep_interval_seconds = std::max(15, j.value("sweep_interval_seconds", 60));
    }

    void to_json(nlohmann::json &j, const StatsSnapshotsConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"runtime_interval_seconds", c.runtime_interval_seconds},
            {"gauge_observation_interval_seconds", c.gauge_observation_interval_seconds},
            {"vault_interval_seconds", c.vault_interval_seconds},
            {"retention_days", c.retention_days}
        };
    }

    void from_json(const nlohmann::json &j, StatsSnapshotsConfig &c) {
        c.enabled = j.value("enabled", true);
        c.runtime_interval_seconds = 60u;
        c.gauge_observation_interval_seconds = std::clamp(j.value("gauge_observation_interval_seconds", 3u), 1u, 60u);
        c.vault_interval_seconds = std::max(300u, j.value("vault_interval_seconds", 3600u));
        c.retention_days = std::max(1u, j.value("retention_days", 30u));
    }

    void to_json(nlohmann::json &j, const ServicesConfig &c) {
        j = {
            {"db_sweeper", c.db_sweeper},
            {"connection_lifecycle_manager", c.connection_lifecycle_manager}
        };
    }

    void from_json(const nlohmann::json &j, ServicesConfig &c) {
        j.at("db_sweeper").get_to(c.db_sweeper);
        j.at("connection_lifecycle_manager").get_to(c.connection_lifecycle_manager);
    }

    void to_json(nlohmann::json &j, const SharingConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"enable_public_links", c.enable_public_links}
        };
    }

    void from_json(const nlohmann::json &j, SharingConfig &c) {
        c.enabled = j.value("enabled", true);
        c.enable_public_links = j.value("enable_public_links", true);
    }

    void to_json(nlohmann::json &j, const ResendEmailConfig &c) {
        j = {
            {"endpoint", c.endpoint}
        };
    }

    void from_json(const nlohmann::json &j, ResendEmailConfig &c) {
        c.endpoint = j.value("endpoint", "https://api.resend.com/emails");
    }

    void to_json(nlohmann::json &j, const SesEmailConfig &c) {
        j = {
            {"region", c.region},
            {"endpoint", c.endpoint ? nlohmann::json(*c.endpoint) : nlohmann::json(nullptr)}
        };
    }

    void from_json(const nlohmann::json &j, SesEmailConfig &c) {
        c.region = j.value("region", "us-east-1");
        if (j.contains("endpoint") && !j.at("endpoint").is_null())
            c.endpoint = j.at("endpoint").get<std::string>();
        else
            c.endpoint.reset();
    }

    void to_json(nlohmann::json &j, const EmailConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"provider", emailProviderKindToString(c.provider)},
            {"from", c.from},
            {"reply_to", c.reply_to ? nlohmann::json(*c.reply_to) : nlohmann::json(nullptr)},
            {"base_url", c.base_url ? nlohmann::json(*c.base_url) : nlohmann::json(nullptr)},
            {"resend", c.resend},
            {"ses", c.ses}
        };
    }

    void from_json(const nlohmann::json &j, EmailConfig &c) {
        c.enabled = j.value("enabled", false);
        c.provider = emailProviderKindFromString(j.value("provider", "none"));
        c.from = j.value("from", "Vaulthalla <ops@example.com>");

        if (j.contains("reply_to") && !j.at("reply_to").is_null())
            c.reply_to = j.at("reply_to").get<std::string>();
        else
            c.reply_to.reset();

        if (j.contains("base_url") && !j.at("base_url").is_null())
            c.base_url = j.at("base_url").get<std::string>();
        else
            c.base_url.reset();

        if (j.contains("resend")) j.at("resend").get_to(c.resend);
        if (j.contains("ses")) j.at("ses").get_to(c.ses);
    }

    void to_json(nlohmann::json &j, const OperatorEmailRecipientsConfig &c) {
        j = {
            {"alerts", c.alerts},
            {"weekly", c.weekly},
            {"security", c.security}
        };
    }

    void from_json(const nlohmann::json &j, OperatorEmailRecipientsConfig &c) {
        c.alerts = j.value("alerts", std::vector<std::string>{});
        c.weekly = j.value("weekly", std::vector<std::string>{});
        c.security = j.value("security", std::vector<std::string>{});
    }

    void to_json(nlohmann::json &j, const OperatorEmailAlertingConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"min_severity", c.min_severity},
            {"dedupe_window_minutes", c.dedupe_window_minutes},
            {"repeat_after_hours", c.repeat_after_hours},
            {"send_recovery", c.send_recovery},
            {"health_poll_seconds", c.health_poll_seconds}
        };
    }

    void from_json(const nlohmann::json &j, OperatorEmailAlertingConfig &c) {
        c.enabled = j.value("enabled", true);
        c.min_severity = j.value("min_severity", "warning");
        c.dedupe_window_minutes = std::max(1u, j.value("dedupe_window_minutes", 60u));
        c.repeat_after_hours = std::max(1u, j.value("repeat_after_hours", 24u));
        c.send_recovery = j.value("send_recovery", true);
        c.health_poll_seconds = std::max(15u, j.value("health_poll_seconds", 60u));
    }

    void to_json(nlohmann::json &j, const OperatorEmailDigestConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"weekday", c.weekday},
            {"hour_local", c.hour_local},
            {"timezone", c.timezone}
        };
    }

    void from_json(const nlohmann::json &j, OperatorEmailDigestConfig &c) {
        c.enabled = j.value("enabled", true);
        c.weekday = j.value("weekday", "monday");
        c.hour_local = std::min(23u, j.value("hour_local", 8u));
        c.timezone = j.value("timezone", "UTC");
    }

    void to_json(nlohmann::json &j, const OperatorEmailSecurityAlertsConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"admin_role_changes", c.admin_role_changes}
        };
    }

    void from_json(const nlohmann::json &j, OperatorEmailSecurityAlertsConfig &c) {
        c.enabled = j.value("enabled", true);
        c.admin_role_changes = j.value("admin_role_changes", true);
    }

    void to_json(nlohmann::json &j, const OperatorEmailsConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"recipients", c.recipients},
            {"alerting", c.alerting},
            {"weekly_digest", c.weekly_digest},
            {"security_alerts", c.security_alerts}
        };
    }

    void from_json(const nlohmann::json &j, OperatorEmailsConfig &c) {
        c.enabled = j.value("enabled", true);
        if (j.contains("recipients")) j.at("recipients").get_to(c.recipients);
        if (j.contains("alerting")) j.at("alerting").get_to(c.alerting);
        if (j.contains("weekly_digest")) j.at("weekly_digest").get_to(c.weekly_digest);
        if (j.contains("security_alerts")) j.at("security_alerts").get_to(c.security_alerts);
    }

    void to_json(nlohmann::json &j, const AuditLogConfig &c) {
        j = {
            {"retention_days", c.retention_days.count()},
            {"rotate_max_size", bytesToMbOrGbStr(c.rotate_max_size)},
            {"rotate_interval", hoursToDayOrHourStr(c.rotate_interval)},
            {"compression", compressionToString(c.compression)},
            {"max_retained_logs_size", bytesToMbOrGbStr(c.max_retained_logs_size)},
            {"strict_retention", c.strict_retention}
        };
    }

    void from_json(const nlohmann::json &j, AuditLogConfig &c) {
        c.retention_days = std::chrono::days(j.value("retention_days", 30));
        c.rotate_max_size = parseMbOrGbToByte(j.value("rotate_max_size", "50MB"));
        c.rotate_interval = parseHoursFromDayOrHour(j.value("rotate_interval", "24h"));
        c.compression = parseCompression(j.value("compression", "zstd"));
        c.max_retained_logs_size = parseMbOrGbToByte(j.value("max_retained_logs_size", "1GB"));
        c.strict_retention = j.value("strict_retention", false);
    }

    void to_json(nlohmann::json &j, const EncryptionWaiverConfig &c) {
        j = {
            {"retention_days", c.retention_days.count()}
        };
    }

    void from_json(const nlohmann::json &j, EncryptionWaiverConfig &c) {
        c.retention_days = std::chrono::days(j.value("retention_days", 180));
    }

    void to_json(nlohmann::json &j, const FilesTrashedConfig &c) {
        j = {
            {"retention_days", c.retention_days.count()}
        };
    }

    void from_json(const nlohmann::json &j, FilesTrashedConfig &c) {
        c.retention_days = std::chrono::days(j.value("retention_days", 60));
    }

    void to_json(nlohmann::json &j, const AuditConfig &c) {
        j = {
            {"audit_log", c.audit_log},
            {"encryption_waivers", c.encryption_waivers},
            {"files_trashed", c.files_trashed}
        };
    }

    void from_json(const nlohmann::json &j, AuditConfig &c) {
        j.at("audit_log").get_to(c.audit_log);
        j.at("encryption_waivers").get_to(c.encryption_waivers);
        j.at("files_trashed").get_to(c.files_trashed);
    }

    void to_json(nlohmann::json &j, const DevConfig &c) {
        j = {
            {"enabled", c.enabled},
            {"init_r2_test_vault", c.init_r2_test_vault}
        };
    }

    void from_json(const nlohmann::json &j, DevConfig &c) {
        c.enabled = j.value("enabled", false);
        c.init_r2_test_vault = j.value("init_r2_test_vault", false);
    }
} // namespace vh::config
