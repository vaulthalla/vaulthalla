#include "notifications/OperatorNotificationBus.hpp"

#include "log/Registry.hpp"

#include <algorithm>

namespace vh::notifications {

OperatorNotificationBus& OperatorNotificationBus::instance() {
    static OperatorNotificationBus bus;
    return bus;
}

bool OperatorNotificationBus::enqueue(OperatorNotification notification) {
    std::scoped_lock lock(mutex_);
    if (queue_.size() >= kMaxQueued) {
        log::Registry::runtime()->warn(
            "[OperatorNotificationBus] Dropping notification because queue is full: {}",
            notification.eventKey
        );
        return false;
    }

    queue_.push_back(std::move(notification));
    return true;
}

std::vector<OperatorNotification> OperatorNotificationBus::drain(const std::size_t maxItems) {
    std::vector<OperatorNotification> out;
    if (maxItems == 0) return out;

    std::scoped_lock lock(mutex_);
    const auto count = std::min(maxItems, queue_.size());
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    return out;
}

std::size_t OperatorNotificationBus::size() const {
    std::scoped_lock lock(mutex_);
    return queue_.size();
}

}
