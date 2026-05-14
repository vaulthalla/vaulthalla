#include "email/Message.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vh::email {

namespace {

std::string trim(std::string value) {
    const auto isSpace = [](const unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::ranges::find_if(value, [&](const unsigned char c) { return !isSpace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](const unsigned char c) { return !isSpace(c); }).base(), value.end());
    return value;
}

}

bool isValidEmailAddress(const std::string& value) {
    if (value.empty()) return false;
    if (value.size() > 254) return false;
    if (value.find_first_of("<>\"") != std::string::npos) return false;
    if (std::ranges::any_of(value, [](const unsigned char c) { return std::isspace(c) != 0; })) return false;

    const auto at = value.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= value.size()) return false;
    if (value.find('@', at + 1) != std::string::npos) return false;
    if (value.find('.', at + 1) == std::string::npos) return false;

    return true;
}

Address parseAddress(const std::string& value) {
    const auto trimmed = trim(value);
    const auto open = trimmed.find('<');
    const auto close = trimmed.find('>');

    if (open != std::string::npos || close != std::string::npos) {
        if (open == std::string::npos || close == std::string::npos || close <= open + 1)
            throw std::invalid_argument("invalid email address display form");

        Address out;
        out.name = trim(trimmed.substr(0, open));
        if (out.name->empty()) out.name.reset();
        out.email = trim(trimmed.substr(open + 1, close - open - 1));
        if (!isValidEmailAddress(out.email)) throw std::invalid_argument("invalid email address: " + out.email);
        return out;
    }

    if (!isValidEmailAddress(trimmed)) throw std::invalid_argument("invalid email address: " + trimmed);
    return {.email = trimmed, .name = std::nullopt};
}

std::string formatAddress(const Address& address) {
    if (!isValidEmailAddress(address.email)) throw std::invalid_argument("invalid email address: " + address.email);
    if (address.name && !address.name->empty()) return *address.name + " <" + address.email + ">";
    return address.email;
}

void validateMessage(const Message& message) {
    (void)formatAddress(message.from);
    if (message.to.empty()) throw std::invalid_argument("email message requires at least one recipient");
    for (const auto& recipient : message.to) (void)formatAddress(recipient);
    if (message.replyTo) (void)formatAddress(*message.replyTo);
    if (message.subject.empty()) throw std::invalid_argument("email message subject is required");
    if (message.html.empty() && message.text.empty()) throw std::invalid_argument("email message requires html or text content");
}

}
