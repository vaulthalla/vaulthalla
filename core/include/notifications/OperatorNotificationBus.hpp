#pragma once

#include "notifications/OperatorNotification.hpp"

#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace vh::notifications {

class OperatorNotificationBus {
public:
    static OperatorNotificationBus& instance();

    [[nodiscard]] bool enqueue(OperatorNotification notification);
    [[nodiscard]] std::vector<OperatorNotification> drain(std::size_t maxItems);
    [[nodiscard]] std::size_t size() const;

private:
    static constexpr std::size_t kMaxQueued = 512;

    mutable std::mutex mutex_;
    std::deque<OperatorNotification> queue_;
};

}
