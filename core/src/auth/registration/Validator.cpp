#include "auth/registration/Validator.hpp"
#include "crypto/password/Strength.hpp"
#include "identities/User.hpp"
#include "log/Registry.hpp"

#include <paths.h>
#include <ranges>
#include <algorithm>
#include <vector>

namespace vh::auth::registration {

void Validator::validateRegistration(const std::shared_ptr<identities::User>& user, const std::string& password) {
    std::vector<std::string> errors;

    if (!isValidName(user->name)) errors.emplace_back("Name must be between 3 and 50 characters.");

    if (user->email && !isValidEmail(*user->email)) errors.emplace_back("Email must be valid and contain '@' and '.'.");

    if (!paths::testMode) {
        if (const auto strength = crypto::password::Strength::passwordStrengthCheck(password) < 50)
            errors.emplace_back("Password is too weak (strength " + std::to_string(strength) +
                                "/100). Use at least 12 characters, mix upper/lowercase, digits, and symbols.");

        if (crypto::password::Strength::containsDictionaryWord(password)) errors.emplace_back(
            "Password contains dictionary word — this is forbidden.");

        if (crypto::password::Strength::isCommonWeakPassword(password)) errors.emplace_back(
            "Password matches known weak pattern — this is forbidden.");

        if (crypto::password::Strength::isPwnedPassword(password)) errors.emplace_back(
            "Password has been found in public breaches — choose a different one.");
    }

    if (!errors.empty()) {
        std::ostringstream oss;
        oss << "Registration failed due to the following issues:\n";
        for (const auto& err : errors) oss << "- " << err << std::endl;
        log::Registry::auth()->error("[AuthManager] Registration validation failed: {}", oss.str());
        throw std::runtime_error(oss.str());
    }
}

bool Validator::isValidName(const std::string& name) {
    return !name.empty() && name.size() > 2 && name.size() <= 50;
}

bool Validator::isValidEmail(const std::string& email) {
    return !email.empty() && email.find('@') != std::string::npos && email.find('.') != std::string::npos;
}

bool Validator::isValidPassword(const std::string& password) {
    if (paths::testMode) return true;
    std::vector<std::string> errors;
    if (crypto::password::Strength::passwordStrengthCheck(password) < 50) return false;
    if (crypto::password::Strength::containsDictionaryWord(password)) return false;
    if (crypto::password::Strength::isCommonWeakPassword(password)) return false;
    if (crypto::password::Strength::isPwnedPassword(password)) return false;
    return !password.empty() && password.size() >= 8 && password.size() <= 128 &&
           std::ranges::any_of(password.begin(), password.end(), ::isdigit) && // At least one digit
           std::ranges::any_of(password.begin(), password.end(), ::isalpha);   // At least one letter
}

bool Validator::isValidGroup(const std::string& group) {
    return !group.empty() && group.size() >= 3 && group.size() <= 50;
}

}
