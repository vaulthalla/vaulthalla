#pragma once

#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; class result; }

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
    unsigned int id{};
    std::optional<unsigned int> linux_gid{std::nullopt};
    std::string name;
    std::optional<std::string> description{std::nullopt};
    std::time_t created_at{};
    std::optional<std::time_t> updated_at{std::nullopt};
    std::vector<std::shared_ptr<GroupMember>> members{};

    Group() = default;
    explicit Group(const pqxx::row& gr, const pqxx::result& members);
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
