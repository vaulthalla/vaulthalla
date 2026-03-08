#include "log/RotationService.hpp"
#include "log/Registry.hpp"
#include "log/Rotator.hpp"
#include "config/Registry.hpp"

#include <paths.h>

using namespace vh::log;

static constexpr auto CHECK_INTERVAL = std::chrono::minutes(10);

RotationService::RotationService()
    : AsyncService("LogRotationService"),
      appRot_(std::make_unique<Rotator>(Rotator::Options{
        .active_path = paths::getLogPath() / "vaulthalla.log",
        .max_bytes = 25_MiB,
        .max_interval = std::chrono::hours(24),
        .retention_days = std::chrono::days(7),
        .max_retained_size = 100_MiB,
        .compression = Rotator::Compression::Zstd,
        .ignore_compress_errors = true,
        .on_reopen = []() { Registry::reopenMainLog(); },
        .diag_log = [](const std::string_view& msg) { Registry::vaulthalla()->debug("[LogRotator] {}", msg); }
      })),
      auditRot_(std::make_unique<Rotator>(Rotator::Options{
        .active_path = paths::getLogPath() / "audit.log",
        .max_bytes = config::Registry::get().auditing.audit_log.rotate_max_size,
        .max_interval = config::Registry::get().auditing.audit_log.rotate_interval,
        .retention_days = config::Registry::get().auditing.audit_log.retention_days,
        .max_retained_size = config::Registry::get().auditing.audit_log.max_retained_logs_size,
        .strict_retention = config::Registry::get().auditing.audit_log.strict_retention,
        .compression = config::Registry::get().auditing.audit_log.compression,
        .ignore_compress_errors = true,
        .on_reopen = []() { Registry::reopenAuditLog(); },
        .diag_log = [](const std::string_view& msg) { Registry::audit()->debug("[AuditLogRotator] {}", msg); }
      })) {}

RotationService::~RotationService() { stop(); }

void RotationService::runLoop() {
    while (!shouldStop()) {
        lazySleep(CHECK_INTERVAL);
        Registry::vaulthalla()->debug("[LogRotationService] Checking for log rotation...");
        appRot_->maybeRotate();
        auditRot_->maybeRotate();
    }
}
