#pragma once

#include "email/DeliveryHistory.hpp"
#include "email/Provider.hpp"
#include "notifications/OperatorNotification.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vh::notifications {

struct OperatorNotificationState {
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
