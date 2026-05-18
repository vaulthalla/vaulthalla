#include "notifications/OperatorEmailService.hpp"

#include "config/Registry.hpp"
#include "email/Message.hpp"
#include "email/ProviderFactory.hpp"
#include "email/templates/OperatorTemplates.hpp"
#include "log/Registry.hpp"
#include "notifications/OperatorNotificationBus.hpp"
#include "notifications/OperatorNotificationState.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/SystemHealth.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <sstream>
#include <unistd.h>

namespace vh::notifications {

namespace {

constexpr auto kIdleSleep = std::chrono::seconds(5);
constexpr std::size_t kBatchSize = 25;
constexpr const char* kWatchdogEventKey = "watchdog.system_health";
constexpr const char* kWatchdogRecoveryEventKey = "watchdog.system_health.recovery";

std::string instanceName() {
    char host[256]{};
    if (::gethostname(host, sizeof(host) - 1) == 0 && host[0] != '\0')
        return host;
    return "vaulthalla";
}

std::vector<std::string> recipientsForGroup(
    const config::OperatorEmailRecipientsConfig& recipients,
    const std::string& group
) {
    if (group == "weekly") return recipients.weekly;
    if (group == "security") return recipients.security;
    return recipients.alerts;
}

std::string idempotencyKeyFor(const OperatorNotification& notification) {
    std::ostringstream out;
    out << "operator:" << (notification.eventType.empty() ? "event" : notification.eventType)
        << ":" << (notification.eventKey.empty() ? "notification" : notification.eventKey)
        << ":" << (notification.fingerprint.empty() ? "default" : notification.fingerprint);
    return out.str();
}

int severityRank(const std::string& severity) {
    if (severity == "critical") return 3;
    if (severity == "warning") return 2;
    if (severity == "info") return 1;
    return 2;
}

bool severityAllowed(const std::string& severity, const std::string& minSeverity) {
    return severityRank(severity) >= severityRank(minSeverity);
}

std::string severityFor(const stats::model::SystemHealth& health) {
    return health.overallStatus == stats::model::SystemHealthStatus::Critical ? "critical" : "warning";
}

std::vector<std::string> failedServices(const stats::model::SystemHealth& health) {
    std::vector<std::string> out;
    for (const auto& service : health.runtime.services) {
        if (!service.running)
            out.push_back(service.entryName.empty() ? service.serviceName : service.entryName);
    }
    return out;
}

std::vector<std::string> missingDependencies(const stats::model::SystemHealth& health) {
    std::vector<std::string> out;
    const auto& deps = health.deps;
    if (!deps.storageManager) out.push_back("storage manager");
    if (!deps.apiKeyManager) out.push_back("API key manager");
    if (!deps.authManager) out.push_back("auth manager");
    if (!deps.sessionManager) out.push_back("session manager");
    if (!deps.secretsManager) out.push_back("secrets manager");
    if (!deps.syncController) out.push_back("sync controller");
    if (!deps.fsCache) out.push_back("filesystem cache");
    if (!deps.shellUsageManager) out.push_back("shell usage manager");
    if (!deps.httpCacheStats) out.push_back("HTTP cache stats");
    if (!deps.fuseSession) out.push_back("FUSE session");
    return out;
}

std::vector<std::string> protocolIssues(const stats::model::SystemHealth& health) {
    std::vector<std::string> out;
    const auto& protocols = health.protocols;
    if (!protocols.running) out.push_back("protocol service is not running");
    if (!protocols.ioContextInitialized) out.push_back("protocol IO context is not initialized");
    if (protocols.websocketConfigured && !protocols.websocketReady) out.push_back("websocket endpoint is not ready");
    if (protocols.httpPreviewConfigured && !protocols.httpPreviewReady) out.push_back("HTTP preview endpoint is not ready");
    return out;
}

std::string joinSorted(std::vector<std::string> values) {
    std::ranges::sort(values);
    std::ostringstream out;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it != values.begin()) out << ",";
        out << *it;
    }
    return out.str();
}

std::string fingerprintFor(
    const stats::model::SystemHealth& health,
    const std::string& severity,
    const std::vector<std::string>& services,
    const std::vector<std::string>& deps,
    const std::vector<std::string>& protocols
) {
    std::ostringstream out;
    out << severity
        << "|status:" << health.overallStatusString()
        << "|services:" << joinSorted(services)
        << "|deps:" << joinSorted(deps)
        << "|protocols:" << joinSorted(protocols);
    return out.str();
}

email::templates::WatchdogEmailContext watchdogContext(
    const stats::model::SystemHealth& health,
    const std::string& severity,
    const std::string& fingerprint,
    const std::vector<std::string>& services,
    const std::vector<std::string>& deps,
    const std::vector<std::string>& protocols
) {
    return {
        .instance = instanceName(),
        .status = health.overallStatusString(),
        .severity = severity,
        .fingerprint = fingerprint,
        .checkedAt = health.summary.checkedAt,
        .servicesReady = health.summary.servicesReady,
        .servicesTotal = health.summary.servicesTotal,
        .depsReady = health.summary.depsReady,
        .depsTotal = health.summary.depsTotal,
        .protocolsReady = health.summary.protocolsReady,
        .protocolsTotal = health.summary.protocolsTotal,
        .failedServices = services,
        .missingDependencies = deps,
        .protocolIssues = protocols,
        .baseUrl = config::Registry::get().email.base_url
    };
}

OperatorNotificationPolicy alertPolicyFromConfig(const config::OperatorEmailAlertingConfig& config) {
    return {
        .dedupeWindowMinutes = config.dedupe_window_minutes,
        .repeatAfterHours = config.repeat_after_hours,
        .sendRecovery = config.send_recovery
    };
}

}

OperatorEmailService::OperatorEmailService()
    : AsyncService("OperatorEmailService") {}

void OperatorEmailService::runLoop() {
    while (!shouldStop()) {
        processBatch();
        evaluateWatchdogIfDue();
        processBatch();
        lazySleep(kIdleSleep);
    }
}

void OperatorEmailService::processBatch() {
    const auto batch = OperatorNotificationBus::instance().drain(kBatchSize);
    for (const auto& notification : batch) {
        if (shouldStop()) break;
        try {
            deliver(notification);
        } catch (const std::exception& e) {
            log::Registry::runtime()->error(
                "[OperatorEmailService] Failed to process notification {}: {}",
                notification.eventKey,
                e.what()
            );
            try {
                OperatorNotificationState::recordFailed(notification, providerName(), 0, e.what());
            } catch (const std::exception& recordError) {
                log::Registry::runtime()->error(
                    "[OperatorEmailService] Failed to record notification delivery failure: {}",
                    recordError.what()
                );
            }
        }
    }
}

void OperatorEmailService::deliver(const OperatorNotification& notification) {
    const auto& cfg = config::Registry::get();
    const auto provider = providerName();

    if (!cfg.operator_emails.enabled) {
        OperatorNotificationState::recordSuppressed(notification, provider, 0, "operator emails are disabled");
        return;
    }
    if (!cfg.email.enabled) {
        OperatorNotificationState::recordSuppressed(notification, provider, 0, "email is disabled");
        return;
    }
    if (cfg.email.provider == config::EmailProviderKind::None) {
        OperatorNotificationState::recordSuppressed(notification, provider, 0, "email provider is none");
        return;
    }
    if (!runtime::Deps::get().secretsManager) {
        OperatorNotificationState::recordFailed(notification, provider, 0, "secrets manager is unavailable");
        return;
    }

    const auto recipients = recipientsFor(notification);
    if (recipients.empty()) {
        OperatorNotificationState::recordSuppressed(notification, provider, 0, "no recipients configured");
        return;
    }

    try {
        email::Message message{
            .from = email::parseAddress(cfg.email.from),
            .to = {},
            .replyTo = cfg.email.reply_to ? std::optional(email::parseAddress(*cfg.email.reply_to)) : std::nullopt,
            .subject = notification.rendered.subject,
            .html = notification.rendered.html,
            .text = notification.rendered.text,
            .idempotencyKey = idempotencyKeyFor(notification),
            .tags = notification.tags
        };
        message.tags.try_emplace("event_type", notification.eventType.empty() ? "operator" : notification.eventType);
        message.tags.try_emplace("severity", notification.severity.empty() ? "info" : notification.severity);

        message.to.reserve(recipients.size());
        for (const auto& recipient : recipients)
            message.to.push_back(email::parseAddress(recipient));

        email::validateMessage(message);

        auto emailProvider = email::makeProvider(cfg.email, runtime::Deps::get().secretsManager);
        if (!emailProvider) {
            OperatorNotificationState::recordFailed(notification, provider, static_cast<std::uint32_t>(message.to.size()), "no provider is configured");
            return;
        }

        const auto result = emailProvider->send(message);
        if (result.ok) {
            OperatorNotificationState::recordSent(notification, emailProvider->name(), static_cast<std::uint32_t>(message.to.size()), result);
            return;
        }

        std::ostringstream error;
        error << "provider " << emailProvider->name() << " failed";
        if (result.httpStatus) error << " (HTTP " << result.httpStatus << ")";
        if (result.errorSummary) error << ": " << *result.errorSummary;
        OperatorNotificationState::recordFailed(notification, emailProvider->name(), static_cast<std::uint32_t>(message.to.size()), error.str());
    } catch (const std::exception& e) {
        OperatorNotificationState::recordFailed(notification, provider, static_cast<std::uint32_t>(recipients.size()), e.what());
    }
}

void OperatorEmailService::evaluateWatchdogIfDue() {
    const auto now = std::chrono::steady_clock::now();
    const auto interval = healthPollInterval();

    if (!healthPollScheduled_) {
        nextHealthPoll_ = now + interval;
        healthPollScheduled_ = true;
        return;
    }
    if (now < nextHealthPoll_) return;

    nextHealthPoll_ = now + interval;
    try {
        evaluateWatchdog();
    } catch (const std::exception& e) {
        log::Registry::runtime()->warn("[OperatorEmailService] Watchdog alert evaluation failed: {}", e.what());
    }
}

void OperatorEmailService::evaluateWatchdog() {
    if (!watchdogDeliveryConfigured()) return;

    const auto& cfg = config::Registry::get();
    const auto policy = alertPolicyFromConfig(cfg.operator_emails.alerting);
    const auto recipients = static_cast<std::uint32_t>(cfg.operator_emails.recipients.alerts.size());
    const auto health = stats::model::SystemHealth::snapshot();

    if (health.healthy()) {
        if (!policy.sendRecovery) return;

        const auto candidate = OperatorNotificationState::recoveryCandidate(kWatchdogEventKey, kWatchdogRecoveryEventKey);
        if (!candidate) return;

        const auto services = failedServices(health);
        const auto deps = missingDependencies(health);
        const auto protocols = protocolIssues(health);
        auto ctx = watchdogContext(health, "info", candidate->fingerprint, services, deps, protocols);
        auto notification = OperatorNotification{
            .eventKey = kWatchdogRecoveryEventKey,
            .eventType = "watchdog",
            .severity = "info",
            .recipientGroup = "alerts",
            .explicitRecipients = {},
            .fingerprint = candidate->fingerprint,
            .rendered = email::templates::renderWatchdogRecoveryEmail(ctx),
            .tags = {{"event_type", "watchdog"}, {"severity", "info"}}
        };

        const auto decision = OperatorNotificationState::shouldSend(notification, policy);
        if (decision.send) {
            if (!OperatorNotificationBus::instance().enqueue(notification))
                OperatorNotificationState::recordSuppressed(notification, providerName(), recipients, "notification queue full");
        } else if (decision.recordSuppression) {
            OperatorNotificationState::recordSuppressed(notification, providerName(), recipients, decision.reason);
        }
        return;
    }

    const auto severity = severityFor(health);
    if (!severityAllowed(severity, cfg.operator_emails.alerting.min_severity)) return;

    const auto services = failedServices(health);
    const auto deps = missingDependencies(health);
    const auto protocols = protocolIssues(health);
    const auto fingerprint = fingerprintFor(health, severity, services, deps, protocols);
    const auto ctx = watchdogContext(health, severity, fingerprint, services, deps, protocols);
    auto notification = OperatorNotification{
        .eventKey = kWatchdogEventKey,
        .eventType = "watchdog",
        .severity = severity,
        .recipientGroup = "alerts",
        .explicitRecipients = {},
        .fingerprint = fingerprint,
        .rendered = email::templates::renderWatchdogAlertEmail(ctx),
        .tags = {{"event_type", "watchdog"}, {"severity", severity}}
    };

    const auto decision = OperatorNotificationState::shouldSend(notification, policy);
    if (decision.send) {
        if (!OperatorNotificationBus::instance().enqueue(notification))
            OperatorNotificationState::recordSuppressed(notification, providerName(), recipients, "notification queue full");
    } else if (decision.recordSuppression) {
        OperatorNotificationState::recordSuppressed(notification, providerName(), recipients, decision.reason);
    }
}

std::chrono::seconds OperatorEmailService::healthPollInterval() const {
    const auto seconds = config::Registry::get().operator_emails.alerting.health_poll_seconds;
    return std::chrono::seconds(std::max<std::uint32_t>(15, seconds));
}

bool OperatorEmailService::watchdogDeliveryConfigured() const {
    const auto& cfg = config::Registry::get();
    return cfg.operator_emails.enabled
        && cfg.operator_emails.alerting.enabled
        && cfg.email.enabled
        && cfg.email.provider != config::EmailProviderKind::None
        && !cfg.operator_emails.recipients.alerts.empty();
}

std::vector<std::string> OperatorEmailService::recipientsFor(const OperatorNotification& notification) const {
    if (!notification.explicitRecipients.empty()) return notification.explicitRecipients;
    return recipientsForGroup(config::Registry::get().operator_emails.recipients, notification.recipientGroup);
}

std::string OperatorEmailService::providerName() const {
    return config::emailProviderKindToString(config::Registry::get().email.provider);
}

}
