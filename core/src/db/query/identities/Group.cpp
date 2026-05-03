#include "db/query/identities/Group.hpp"
#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"
#include "identities/Group.hpp"
#include "db/query/identities/helpers.hpp"

namespace vh::db::query::identities {

using vh::db::encoding::parsePostgresTimestamp;
using vh::db::model::ListQueryParams;

uint32_t Group::createGroup(const GroupPtr& group) {
    return Transactions::exec("Group::createGroup", [&](pqxx::work& txn) {
        const pqxx::params p{group->name, group->description, group->linux_gid};

        const auto res = txn.exec(pqxx::prepped{"create_group"}, p);
        if (res.empty()) throw std::runtime_error("Failed to create group: " + group->name);
        const auto row = res.one_row();

        group->id = row["id"].as<uint32_t>();
        group->created_at = parsePostgresTimestamp(row["created_at"].as<std::string>());
        group->updated_at = parsePostgresTimestamp(row["updated_at"].as<std::string>());

        upsertVaultRoles(txn, group->roles.vaults, "group", group->id);

        return group->id;
    });
}

void Group::updateGroup(const GroupPtr& group) {
    Transactions::exec("Group::updateGroup", [&](pqxx::work& txn) {
        const pqxx::params p{group->id, group->name, group->description, group->linux_gid};
        txn.exec(pqxx::prepped{"update_group"}, p);
        upsertVaultRoles(txn, group->roles.vaults, "group", group->id);
    });
}

void Group::deleteGroup(const uint32_t groupId) {
    Transactions::exec("Group::deleteGroup", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_group"}, groupId);
    });
}

void Group::addMemberToGroup(const uint32_t group, const uint32_t member) {
    Transactions::exec("Group::addMemberToGroup", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"save_group_member"}, pqxx::params{group, member});
    });
}

void Group::addMembersToGroup(const uint32_t group, const std::vector<uint32_t>& members) {
    Transactions::exec("Group::addMembersToGroup", [&](pqxx::work& txn) {
        for (const auto& member : members)
            txn.exec(pqxx::prepped{"save_group_member"}, pqxx::params{group, member});
    });
}

void Group::removeMemberFromGroup(const uint32_t group, const uint32_t member) {
    Transactions::exec("Group::removeMemberFromGroup", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"remove_group_member"}, pqxx::params{group, member});
    });
}

void Group::removeMembersFromGroup(const uint32_t group, const std::vector<uint32_t>& members) {
    Transactions::exec("Group::removeMembersFromGroup", [&](pqxx::work& txn) {
        for (const auto& member : members)
            txn.exec(pqxx::prepped{"remove_group_member"}, pqxx::params{group, member});
    });
}

std::vector<Group::GroupPtr> Group::listGroups(const std::optional<uint32_t>& userId, ListQueryParams&& params) {
    return Transactions::exec("Group::listGroups", [&](pqxx::work& txn) -> std::vector<GroupPtr> {
        std::string sql;

        (void)params; // TODO: add pagination

        pqxx::result res;
        if (userId) res = txn.exec(pqxx::prepped{"list_groups_for_user"}, *userId);
        else res = txn.exec(pqxx::prepped{"list_groups"});
        if (res.empty()) return {};

        std::vector<GroupPtr> groups;
        for (const auto& group : res) groups.push_back(hydrateGroup(txn, group));
        return groups;
    });
}

Group::GroupPtr Group::getGroup(const uint32_t groupId) {
    return Transactions::exec("Group::getGroup", [&](pqxx::work& txn) -> GroupPtr {
        const auto res = txn.exec(pqxx::prepped{"get_group"}, groupId);
        if (res.empty()) return nullptr;
        return hydrateGroup(txn, res.one_row());
    });
}

Group::GroupPtr Group::getGroupByName(const std::string& name) {
    return Transactions::exec("Group::getGroupByName",
        [&](pqxx::work& txn) -> GroupPtr {
            const auto res = txn.exec(pqxx::prepped{"get_group_by_name"}, name);
            if (res.empty()) return nullptr;
            return hydrateGroup(txn, res.one_row());
        });
}

bool Group::groupExists(const std::string& name) {
    return Transactions::exec("Group::groupExists", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"group_exists_by_name"}, name);
        if (res.empty()) return false;
        return res.one_field().as<bool>();
    });
}

Group::GroupPtr Group::getGroupByLinuxGID(uint32_t gid) {
    return Transactions::exec("Group::getGroupByLinuxGID", [&](pqxx::work& txn) -> GroupPtr {
            const auto res = txn.exec(pqxx::prepped{"get_group_by_linux_gid"}, gid);
            if (res.empty()) return nullptr;
            return hydrateGroup(txn, res.one_row());
        });
}

}
