#include "notifications/OperatorNotificationState.hpp"

#include <algorithm>
#include <chrono>
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

std::time_t nowUnix() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

std::time_t recordTime(const email::DeliveryRecord& record) {
    if (record.sentAt) return *record.sentAt;
    return record.createdAt;
}

bool withinSeconds(const std::time_t timestamp, const std::uint64_t seconds) {
    if (timestamp <= 0) return false;
    const auto elapsed = nowUnix() - timestamp;
    return elapsed >= 0 && static_cast<std::uint64_t>(elapsed) < seconds;
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

OperatorNotificationDecision OperatorNotificationState::shouldSend(
    const OperatorNotification& notification,
    const OperatorNotificationPolicy& policy
) {
    const auto latestAny = email::DeliveryHistory::latestFor(
        fallback(notification.eventKey, "operator-notification"),
        fingerprintFor(notification)
    );
    if (latestAny && latestAny->status == "suppressed"
        && withinSeconds(latestAny->createdAt, static_cast<std::uint64_t>(policy.dedupeWindowMinutes) * 60)) {
        return {
            .send = false,
            .reason = latestAny->errorSummary.value_or("suppression already recorded"),
            .recordSuppression = false
        };
    }
    if (latestAny && latestAny->status == "failed"
        && withinSeconds(latestAny->createdAt, static_cast<std::uint64_t>(policy.dedupeWindowMinutes) * 60)) {
        return {
            .send = false,
            .reason = "recent delivery failure backoff",
            .recordSuppression = true
        };
    }

    const auto latestSent = email::DeliveryHistory::latestForStatus(
        fallback(notification.eventKey, "operator-notification"),
        fingerprintFor(notification),
        "sent"
    );
    if (!latestSent) return {.send = true, .reason = "no prior sent delivery"};

    const auto sentAt = recordTime(*latestSent);
    if (withinSeconds(sentAt, static_cast<std::uint64_t>(policy.dedupeWindowMinutes) * 60)) {
        return {
            .send = false,
            .reason = "dedupe window active"
        };
    }

    if (withinSeconds(sentAt, static_cast<std::uint64_t>(policy.repeatAfterHours) * 60 * 60)) {
        return {
            .send = false,
            .reason = "repeat window active"
        };
    }

    return {.send = true, .reason = "repeat window elapsed"};
}

std::optional<email::DeliveryRecord> OperatorNotificationState::recoveryCandidate(
    const std::string& alertEventKey,
    const std::string& recoveryEventKey
) {
    const auto alert = email::DeliveryHistory::latestForEventStatus(alertEventKey, "sent");
    if (!alert) return std::nullopt;

    const auto recovery = email::DeliveryHistory::latestForStatus(recoveryEventKey, alert->fingerprint, "sent");
    if (recovery && recovery->createdAt >= alert->createdAt) return std::nullopt;
    return alert;
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
