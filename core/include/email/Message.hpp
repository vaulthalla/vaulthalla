#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vh::email {

struct Address {
    std::string email;
    std::optional<std::string> name;
};

struct Message {
    Address from;
    std::vector<Address> to;
    std::optional<Address> replyTo;
    std::string subject;
    std::string html;
    std::string text;
    std::string idempotencyKey;
    std::map<std::string, std::string> tags;
};

[[nodiscard]] bool isValidEmailAddress(const std::string& value);
[[nodiscard]] Address parseAddress(const std::string& value);
[[nodiscard]] std::string formatAddress(const Address& address);
void validateMessage(const Message& message);

}
