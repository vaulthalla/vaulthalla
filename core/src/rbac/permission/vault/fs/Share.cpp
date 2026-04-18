#include "rbac/permission/vault/fs/Share.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault::fs {

std::string Share::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Sharing:\n";
    const std::string in(indent + 2, ' ');
    oss << in << "Share Internal: " << bool_to_string(canShareInternally()) << "\n";
    oss << in << "Share Publicly: " << bool_to_string(canSharePublicly()) << "\n";
    oss << in << "Share Publicly with Validation: " << bool_to_string(canSharePubliclyWithValidation()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const Share& s) {
    j = {
        {"internal", s.canShareInternally()},
        {"public", s.canSharePublicly()},
        {"public_with_validation", s.canSharePubliclyWithValidation()}
    };
}

void from_json(const nlohmann::json& j, Share& s) {
    s.clear();
    if (j.at("internal").get<bool>()) s.grant(SharePermissions::Internal);
    if (j.at("public").get<bool>()) s.grant(SharePermissions::Public);
    if (j.at("public_with_validation").get<bool>()) s.grant(SharePermissions::PublicWithValidation);
}

}
