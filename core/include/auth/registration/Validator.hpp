#pragma once

#include <string>
#include <memory>
#include "identities/Fwd.hpp"

namespace vh::auth::registration {

struct Validator {
    static void validateRegistration(const std::shared_ptr<identities::User>& user,
                                     const std::string& password);
    static bool isValidName(const std::string& name);
    static bool isValidEmail(const std::string& email);
    static bool isValidPassword(const std::string& password);
    static bool isValidGroup(const std::string& group);
};

}
