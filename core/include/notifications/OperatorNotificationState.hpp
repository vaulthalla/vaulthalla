#pragma once

#include "email/DeliveryHistory.hpp"
#include "email/Provider.hpp"
#include "notifications/OperatorNotification.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vh::notifications {

struct OperatorNotificationPolicy {
    std::uint32_t dedupeWindowMinutes = 60;
    std::uint32_t repeatAfterHours = 24;
    bool sendRecovery = true;
};

struct OperatorNotificationDecision {
    bool send = false;
    std::string reason;
    bool recordSuppression = true;
};

struct OperatorNotificationState {
    [[nodiscard]] static OperatorNotificationDecision shouldSend(
        const OperatorNotification& notification,
        const OperatorNotificationPolicy& policy
    );
    [[nodiscard]] static std::optional<email::DeliveryRecord> recoveryCandidate(
        const std::string& alertEventKey,
        const std::string& recoveryEventKey
    );

    static void recordDryRun(
        const OperatorNotification& notification,
        const std::string& provider,
        std::uint32_t recipientCount
    );
    static void recordSent(
        const OperatorNotification& notification,
        const std::string& provider,
        std::uint32_t recipientCount,
        const email::SendResult& result
    );
    static void recordFailed(
        const OperatorNotification& notification,
        const std::string& provider,
        std::uint32_t recipientCount,
        const std::string& errorSummary
    );
    static void recordSuppressed(
        const OperatorNotification& notification,
        const std::string& provider,
        std::uint32_t recipientCount,
        const std::string& reason
    );

    [[nodiscard]] static std::vector<email::DeliveryRecord> history(std::uint32_t limit = 100);
};

}
