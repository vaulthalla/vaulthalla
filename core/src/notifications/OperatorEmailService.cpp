#include "notifications/OperatorEmailService.hpp"

#include "config/Registry.hpp"
#include "email/Message.hpp"
#include "email/ProviderFactory.hpp"
#include "log/Registry.hpp"
#include "notifications/OperatorNotificationBus.hpp"
#include "notifications/OperatorNotificationState.hpp"
#include "runtime/Deps.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <sstream>

namespace vh::notifications {

namespace {

constexpr auto kIdleSleep = std::chrono::seconds(5);
constexpr std::size_t kBatchSize = 25;

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

}

OperatorEmailService::OperatorEmailService()
    : AsyncService("OperatorEmailService") {}

void OperatorEmailService::runLoop() {
    while (!shouldStop()) {
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

std::vector<std::string> OperatorEmailService::recipientsFor(const OperatorNotification& notification) const {
    if (!notification.explicitRecipients.empty()) return notification.explicitRecipients;
    return recipientsForGroup(config::Registry::get().operator_emails.recipients, notification.recipientGroup);
}

std::string OperatorEmailService::providerName() const {
    return config::emailProviderKindToString(config::Registry::get().email.provider);
}

}
