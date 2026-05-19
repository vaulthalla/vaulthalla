#pragma once

#include "email/RenderedEmail.hpp"

#include <map>
#include <string>
#include <vector>

namespace vh::notifications {

struct OperatorNotification {
    std::string eventKey;
    std::string eventType;
    std::string severity = "info";
    std::string recipientGroup = "alerts";
    std::vector<std::string> explicitRecipients;
    std::string fingerprint;
    email::RenderedEmail rendered;
    std::map<std::string, std::string> tags;
};

}
