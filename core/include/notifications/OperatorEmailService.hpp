#pragma once

#include "concurrency/AsyncService.hpp"
#include "notifications/OperatorNotification.hpp"

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

    [[nodiscard]] std::vector<std::string> recipientsFor(const OperatorNotification& notification) const;
    [[nodiscard]] std::string providerName() const;
};

}
