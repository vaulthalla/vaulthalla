#include "identities/Group.hpp"
#include "identities/User.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/util/lineHelpers.hpp"
#include "rbac/role/Vault.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>

using namespace vh::protocols::shell;
using namespace vh::db::encoding;

namespace vh::identities {

GroupMember::GroupMember(const pqxx::row& row)
    : user(std::make_shared<User>(row)),
      joined_at(parsePostgresTimestamp(row["joined_at"].as<std::string>())) {}

Group::Group(const pqxx::row& gr, const pqxx::result& members, std::unordered_map<uint32_t, std::shared_ptr<rbac::role::Vault>>&& vRoles)
    : id(gr["id"].as<unsigned int>()),
      linux_gid(gr["linux_gid"].as<std::optional<unsigned int>>()),
      name(gr["name"].as<std::string>()),
      description(gr["description"].is_null() ? std::nullopt : std::make_optional(gr["description"].as<std::string>())),
      created_at(parsePostgresTimestamp(gr["created_at"].as<std::string>())),
      updated_at(gr["updated_at"].is_null()
                     ? std::nullopt
                     : std::make_optional(parsePostgresTimestamp(gr["updated_at"].as<std::string>()))) {
    for (const auto& memberRow : members) this->members.push_back(std::make_shared<GroupMember>(memberRow));
    roles.vaults = std::move(vRoles);
}

Group::Group(const nlohmann::json& j)
    : id(j.at("id").get<unsigned int>()),
      linux_gid(j.contains("gid") ? j.at("gid").get<unsigned int>() : 0),
      name(j.at("name").get<std::string>()),
      description(j.contains("description")
                      ? std::make_optional(j.at("description").get<std::string>())
                      : std::nullopt),
      created_at(parsePostgresTimestamp(j.at("created_at").get<std::string>())),
      updated_at(j.contains("updated_at") && !j["updated_at"].is_null()
                     ? std::make_optional(parsePostgresTimestamp(j.at("updated_at").get<std::string>()))
                     : std::nullopt) {
    for (const auto& memberJson : j.at("members")) {
        auto member = std::make_shared<GroupMember>();
        member->user = std::make_shared<User>();
        member->user->id = memberJson.at("user_id").get<unsigned int>();
        member->joined_at = parsePostgresTimestamp(memberJson.at("joined_at").get<std::string>());
        members.push_back(member);
    }
}

void to_json(nlohmann::json& j, const Group& g) {
    nlohmann::json members = nlohmann::json::array();
    for (const auto& member : g.members) members.push_back(*member);

    j = {
        {"id", g.id},
        {"name", g.name},
        {"description", g.description},
        {"created_at", timestampToString(g.created_at)},
        {"members", members}
    };

    if (g.linux_gid) j["gid"] = *g.linux_gid;

    if (g.updated_at.has_value()) j["updated_at"] = timestampToString(*g.updated_at);
}

void from_json(const nlohmann::json& j, Group& g) {
    g.id = j.at("id").get<unsigned int>();
    g.linux_gid = j.contains("gid") ? j.at("gid").get<unsigned int>() : 0;
    g.name = j.at("name").get<std::string>();
    g.description = j.contains("description")
                        ? std::make_optional(j.at("description").get<std::string>())
                        : std::nullopt;
    g.created_at = parsePostgresTimestamp(j.at("created_at").get<std::string>());
    g.updated_at = j.contains("updated_at") && !j["updated_at"].is_null()
                       ? std::make_optional(parsePostgresTimestamp(j.at("updated_at").get<std::string>()))
                       : std::nullopt;

    for (const auto& memberJson : j.at("members")) {
        auto member = std::make_shared<GroupMember>();
        member->user = std::make_shared<User>();
        member->user->id = memberJson.at("user_id").get<unsigned int>();
        member->joined_at = parsePostgresTimestamp(memberJson.at("joined_at").get<std::string>());
        g.members.push_back(member);
    }
}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Group>>& groups) {
    j = nlohmann::json::array();
    for (const auto& group : groups) j.emplace_back(*group);
}

std::vector<std::shared_ptr<Group> > groups_from_json(const nlohmann::json& j) {
    std::vector<std::shared_ptr<Group> > groups;
    for (const auto& groupJson : j) groups.push_back(std::make_shared<Group>(groupJson));
    return groups;
}

void to_json(nlohmann::json& j, const GroupMember& gm) {
    j = {
        {"user", to_public_json(gm.user)},
        {"joined_at", timestampToString(gm.joined_at)}
    };
}

std::string to_string(const std::shared_ptr<Group>& g) {
    std::string out;
    out += "Group Name: " + g->name + "\n";
    out += "Group ID: " + std::to_string(g->id) + "\n";
    if (g->description) out += "\nDescription: " + *g->description;
    out += "\nCreated at: " + timestampToString(g->created_at);
    if (g->updated_at) out += "\nUpdated at: " + timestampToString(*g->updated_at);

    out += "\nMembers:\n";
    for (const auto& member : g->members)
        out += "  - " + member->user->name + " (ID: " + std::to_string(member->user->id) + "), Joined at: " + timestampToString(member->joined_at) + "\n";

    return out;
}

std::string to_string(const std::vector<std::shared_ptr<Group>>& groups) {
    if (groups.empty()) return "No groups found";

    Table tbl({
        {"ID", Align::Left, 4, 8, false, false},
        {"Name", Align::Left, 8, 30, true, true},
        {"Description", Align::Left, 30, 50, true, true},
        {"Created At", Align::Left, 20, 30, true, true},
        {"Members", Align::Left, 4, 10, false, false}
    }, term_width());

    for (const auto& g : groups) {
        tbl.add_row({
            std::to_string(g->id),
            g->name,
            g->description.value_or(""),
            timestampToString(g->created_at),
            std::to_string(g->members.size())
        });
    }

    return tbl.render();
}

std::string to_string(const std::shared_ptr<GroupMember>& gm) {
    return "Member: " + gm->user->name + " (ID: " + std::to_string(gm->user->id) + "), Joined at: " + timestampToString(gm->joined_at);
}

std::string to_string(const std::vector<std::shared_ptr<GroupMember>>& members) {
    if (members.empty()) return "No members found";

    std::string out = "Group Members:\n";
    for (const auto& member : members) out += "  - " + to_string(member) + "\n";
    return out;
}

}
