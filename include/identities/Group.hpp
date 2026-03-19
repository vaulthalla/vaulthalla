#pragma once

#include <ctime>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; class result; }
namespace vh::rbac::role { struct Vault; }

namespace vh::identities {

struct User;
struct Volume;

struct GroupMember {
    std::shared_ptr<User> user;
    std::time_t joined_at{};

    GroupMember() = default;
    explicit GroupMember(const pqxx::row& row);
};

struct Group {
    struct RoleAssignments {
        std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>> vaults{};
    };

    unsigned int id{};
    std::optional<unsigned int> linux_gid{std::nullopt};
    std::string name;
    std::optional<std::string> description{std::nullopt};
    std::time_t created_at{};
    std::optional<std::time_t> updated_at{std::nullopt};

    std::vector<std::shared_ptr<GroupMember>> members{};
    RoleAssignments roles{};

    Group() = default;
    Group(const pqxx::row& gr, const pqxx::result& members, std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>>&& vRoles);
    explicit Group(const nlohmann::json& j);
};

void to_json(nlohmann::json& j, const Group& g);
void from_json(const nlohmann::json& j, Group& g);

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Group>>& groups);
std::vector<std::shared_ptr<Group>> groups_from_json(const nlohmann::json& j);

void to_json(nlohmann::json& j, const GroupMember& gm);

std::string to_string(const std::shared_ptr<Group>& g);
std::string to_string(const std::vector<std::shared_ptr<Group>>& groups);
std::string to_string(const std::shared_ptr<GroupMember>& gm);
std::string to_string(const std::vector<std::shared_ptr<GroupMember>>& members);

}
