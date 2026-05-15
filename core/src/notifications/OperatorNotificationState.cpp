#include "notifications/OperatorNotificationState.hpp"

#include <algorithm>
#include <optional>

namespace vh::notifications {

namespace {

std::string safeSummary(std::string value) {
    constexpr std::size_t kMax = 300;
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    if (value.size() > kMax) value = value.substr(0, kMax) + "...";
    return value;
}

std::string fallback(const std::string& value, const std::string& fallbackValue) {
    return value.empty() ? fallbackValue : value;
}

std::string fingerprintFor(const OperatorNotification& notification) {
    if (!notification.fingerprint.empty()) return notification.fingerprint;
    if (!notification.eventKey.empty()) return notification.eventKey;
    return "operator-notification";
}

email::DeliveryRecordInput baseRecord(
    const OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const std::string& status
) {
    return {
        .eventKey = fallback(notification.eventKey, "operator-notification"),
        .eventType = fallback(notification.eventType, "operator"),
        .severity = fallback(notification.severity, "info"),
        .provider = fallback(provider, "none"),
        .subject = fallback(notification.rendered.subject, "(no subject)"),
        .recipientGroup = notification.recipientGroup.empty()
            ? std::optional<std::string>{}
            : std::optional<std::string>{notification.recipientGroup},
        .recipientCount = recipientCount,
        .providerMessageId = std::nullopt,
        .status = status,
        .errorSummary = std::nullopt,
        .fingerprint = fingerprintFor(notification)
    };
}

}

void OperatorNotificationState::recordDryRun(
    const OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount
) {
    auto record = baseRecord(notification, provider, recipientCount, "dry_run");
    email::DeliveryHistory::record(record);
}

void OperatorNotificationState::recordSent(
    const OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const email::SendResult& result
) {
    auto record = baseRecord(notification, provider, recipientCount, "sent");
    record.providerMessageId = result.providerMessageId;
    email::DeliveryHistory::record(record);
}

void OperatorNotificationState::recordFailed(
    const OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const std::string& errorSummary
) {
    auto record = baseRecord(notification, provider, recipientCount, "failed");
    record.errorSummary = safeSummary(errorSummary);
    email::DeliveryHistory::record(record);
}

void OperatorNotificationState::recordSuppressed(
    const OperatorNotification& notification,
    const std::string& provider,
    const std::uint32_t recipientCount,
    const std::string& reason
) {
    auto record = baseRecord(notification, provider, recipientCount, "suppressed");
    record.errorSummary = safeSummary(reason);
    email::DeliveryHistory::record(record);
}

std::vector<email::DeliveryRecord> OperatorNotificationState::history(const std::uint32_t limit) {
    return email::DeliveryHistory::recent(limit);
}

}
