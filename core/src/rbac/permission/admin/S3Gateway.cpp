#include "rbac/permission/admin/S3Gateway.hpp"

#include <nlohmann/json.hpp>
#include <ostream>
#include <sstream>

namespace vh::rbac::permission::admin {

S3Gateway::Mask S3Gateway::toMask() const {
    return packWithOwn();
}

void S3Gateway::fromMask(const Mask mask) {
    unpackWithOwn(mask);
}

std::string S3Gateway::toFlagsString() const {
    return joinFlagsWithOwn();
}

std::string S3Gateway::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "S3 Gateway:\n";
    const auto in = std::string(indent + 2, ' ');
    oss << in << "View: " << bool_to_string(canView()) << "\n";
    oss << in << "Manage Service: " << bool_to_string(canManageService()) << "\n";
    oss << in << "Manage Credentials: " << bool_to_string(canManageCredentials()) << "\n";
    oss << in << "Assign Principal: " << bool_to_string(canAssignPrincipal()) << "\n";
    oss << in << "Manage Buckets: " << bool_to_string(canManageBuckets()) << "\n";
    oss << in << "Manage Budgets: " << bool_to_string(canManageBudgets()) << "\n";
    return oss.str();
}

void to_json(nlohmann::json& j, const S3Gateway& s) {
    j = {
        {"view", s.canView()},
        {"manage_service", s.canManageService()},
        {"manage_credentials", s.canManageCredentials()},
        {"assign_principal", s.canAssignPrincipal()},
        {"manage_buckets", s.canManageBuckets()},
        {"manage_budgets", s.canManageBudgets()}
    };
}

void from_json(const nlohmann::json& j, S3Gateway& s) {
    s.clear();
    if (j.at("view").get<bool>()) s.grant(S3GatewayPermissions::View);
    if (j.at("manage_service").get<bool>()) s.grant(S3GatewayPermissions::ManageService);
    if (j.at("manage_credentials").get<bool>()) s.grant(S3GatewayPermissions::ManageCredentials);
    if (j.at("assign_principal").get<bool>()) s.grant(S3GatewayPermissions::AssignPrincipal);
    if (j.at("manage_buckets").get<bool>()) s.grant(S3GatewayPermissions::ManageBuckets);
    if (j.at("manage_budgets").get<bool>()) s.grant(S3GatewayPermissions::ManageBudgets);
}

} // namespace vh::rbac::permission::admin
