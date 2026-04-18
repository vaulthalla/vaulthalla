#include "rbac/permission/admin/Identities.hpp"

#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::admin {

std::string Identities::toFlagsString() const {
    return joinFlags(users, groups, admins);
}

std::string Identities::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Identities:\n";
    const auto in = std::string(indent + 2, ' ');
    oss << in << "Users:\n" << users.toString(indent + 4);
    oss << in << "Groups:\n" << groups.toString(indent + 4);
    oss << in << "Admins:\n" << admins.toString(indent + 4);
    return oss.str();
}

bool Identities::canAdd(const Type type) const noexcept {
    return visit(type, [](const auto& node) { return node.canAdd(); });
}

bool Identities::canView(const Type type) const noexcept {
    return visit(type, [](const auto& node) { return node.canView(); });
}

bool Identities::canDelete(const Type type) const noexcept {
    return visit(type, [](const auto& node) { return node.canDelete(); });
}

bool Identities::canEdit(const Type type) const noexcept {
    return visit(type, [](const auto& node) { return node.canEdit(); });
}

bool Identities::canAddMember(const Type type) const noexcept {
    return visit(type, [](const auto& node) {
            if constexpr (requires { node.canAddMember(); })
                return node.canAddMember();
            else
                return false;
        });
}

bool Identities::canRemoveMember(const Type type) const noexcept {
    return visit(type, [](const auto& node) {
            if constexpr (requires { node.canRemoveMember(); })
                return node.canRemoveMember();
            else
                return false;
        });
}

void to_json(nlohmann::json& j, const Identities& identities) {
    j = {
        {"users", identities.users},
        {"groups", identities.groups},
        {"admins", identities.admins}
    };
}

void from_json(const nlohmann::json& j, Identities& identities) {
    j.at("users").get_to(identities.users);
    j.at("groups").get_to(identities.groups);
    j.at("admins").get_to(identities.admins);
}

}
