#include "db/query/identities/Group.hpp"
#include "db/Transactions.hpp"
#include "identities/Group.hpp"

using namespace vh::db::model;

namespace vh::db::query::identities {

unsigned int Group::createGroup(const GroupPtr& group) {
    return Transactions::exec("Group::createGroup", [&](pqxx::work& txn) {
        pqxx::params p{group->name, group->description, group->linux_gid};
        const auto res = txn.exec(pqxx::prepped{"insert_group"}, p);
        if (res.empty()) throw std::runtime_error("Failed to create group: " + group->name);
        return res.one_field().as<unsigned int>();
    });
}

void Group::updateGroup(const GroupPtr& group) {
    Transactions::exec("Group::updateGroup", [&](pqxx::work& txn) {
        pqxx::params p{group->id, group->name, group->description, group->linux_gid};
        txn.exec(pqxx::prepped{"update_group"}, p);
    });
}

void Group::deleteGroup(const unsigned int groupId) {
    Transactions::exec("Group::deleteGroup", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"delete_group"}, groupId);
    });
}

void Group::addMemberToGroup(const unsigned int group, const unsigned int member) {
    Transactions::exec("Group::addMemberToGroup", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"add_member_to_group"}, pqxx::params{group, member});
    });
}

void Group::addMembersToGroup(const unsigned int group, const std::vector<unsigned int>& members) {
    Transactions::exec("Group::addMembersToGroup", [&](pqxx::work& txn) {
        for (const auto& member : members)
            txn.exec(pqxx::prepped{"add_member_to_group"}, pqxx::params{group, member});
    });
}

void Group::removeMemberFromGroup(const unsigned int group, const unsigned int member) {
    Transactions::exec("Group::removeMemberFromGroup", [&](pqxx::work& txn) {
        txn.exec(pqxx::prepped{"remove_member_from_group"}, pqxx::params{group, member});
    });
}

void Group::removeMembersFromGroup(const unsigned int group, const std::vector<unsigned int>& members) {
    Transactions::exec("Group::removeMembersFromGroup", [&](pqxx::work& txn) {
        for (const auto& member : members)
            txn.exec(pqxx::prepped{"remove_member_from_group"}, pqxx::params{group, member});
    });
}

std::vector<Group::GroupPtr> Group::listGroups(const std::optional<unsigned int>& userId, ListQueryParams&& params) {
    return Transactions::exec("Group::listGroups", [&](pqxx::work& txn) -> std::vector<GroupPtr> {
        std::string sql;

        if (!userId) {
            sql = appendPaginationAndFilter(
                "SELECT * FROM groups",
                params, "id", "name"
            );
        } else {
            sql = appendPaginationAndFilter(
                "SELECT g.* FROM groups g "
                "JOIN group_members gm ON g.id = gm.group_id "
                "WHERE gm.user_id = " + txn.quote(*userId),
                params, "g.id", "g.name"
            );
        }

        const auto res = txn.exec(sql);

        std::vector<GroupPtr> groups;
        for (const auto& group : res) {
            const auto groupId = group["id"].as<unsigned int>();
            const auto members = txn.exec(pqxx::prepped{"list_group_members"}, groupId);
            groups.push_back(std::make_shared<G>(group, members));
        }
        return groups;
    });
}

Group::GroupPtr Group::getGroup(const unsigned int groupId) {
    return Transactions::exec("Group::getGroup", [&](pqxx::work& txn) -> GroupPtr {
        const auto res = txn.exec(pqxx::prepped{"get_group"}, groupId);
        if (res.empty()) return nullptr;
        const auto members = txn.exec(pqxx::prepped{"list_group_members"}, groupId);
        return std::make_shared<G>(res.one_row(), members);
    });
}

Group::GroupPtr Group::getGroupByName(const std::string& name) {
    return Transactions::exec("Group::getGroupByName",
        [&](pqxx::work& txn) -> GroupPtr {
            const auto res = txn.exec(pqxx::prepped{"get_group_by_name"}, name);
            if (res.empty()) return nullptr;
            const auto groupRow = res.one_row();
            const auto groupId = groupRow["id"].as<unsigned int>();
            const auto members = txn.exec(pqxx::prepped{"list_group_members"}, groupId);
            return std::make_shared<G>(groupRow, members);
        });
}

bool Group::groupExists(const std::string& name) {
    return Transactions::exec("Group::groupExists", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"group_exists"}, name);
        if (res.empty()) return false;
        return res.one_field().as<bool>();
    });
}

}
