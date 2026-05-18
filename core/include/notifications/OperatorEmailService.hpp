#pragma once

#include "concurrency/AsyncService.hpp"
#include "notifications/OperatorNotification.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace vh::notifications {

class OperatorEmailService final : public concurrency::AsyncService {
public:
    OperatorEmailService();

private:
    void runLoop() override;

    void processBatch();
    void deliver(const OperatorNotification& notification);
    void evaluateWatchdogIfDue();
    void evaluateWatchdog();
    void evaluateWeeklyDigestIfDue();
    void evaluateWeeklyDigest();

    [[nodiscard]] std::chrono::seconds healthPollInterval() const;
    [[nodiscard]] std::chrono::seconds weeklyDigestCheckInterval() const;
    [[nodiscard]] bool watchdogDeliveryConfigured() const;
    [[nodiscard]] bool weeklyDigestDeliveryConfigured() const;
    [[nodiscard]] std::vector<std::string> recipientsFor(const OperatorNotification& notification) const;
    [[nodiscard]] std::string providerName() const;

    std::chrono::steady_clock::time_point nextHealthPoll_{};
    std::chrono::steady_clock::time_point nextWeeklyDigestCheck_{};
    bool healthPollScheduled_ = false;
    bool weeklyDigestCheckScheduled_ = false;
};

}
