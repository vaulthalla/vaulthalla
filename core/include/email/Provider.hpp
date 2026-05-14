#pragma once

#include "email/Message.hpp"

#include <optional>
#include <string>

namespace vh::email {

struct SendResult {
    bool ok = false;
    std::optional<std::string> providerMessageId;
    std::optional<std::string> errorSummary;
    int httpStatus = 0;
};

class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual SendResult send(const Message& message) = 0;
};

}
