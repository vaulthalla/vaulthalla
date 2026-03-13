#include "rbac/permission/vault/Keys.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault {

std::string Keys::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Keys:\n";
    const auto i = indent + 2;
    oss << apiKey.toString(i);
    oss << encryptionKey.toString(i);
    return oss.str();
}

void to_json(nlohmann::json& j, const Keys& k) {
    j = {
        {"api_key", k.apiKey},
        {"encryption_key", k.encryptionKey}
    };
}

void from_json(const nlohmann::json& j, Keys& k) {
    j.at("api_key").get_to(k.apiKey);
    j.at("encryption_key").get_to(k.encryptionKey);
}

}
