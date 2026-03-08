#include "protocols/ws/handler/Groups.hpp"
#include "protocols/ws/Session.hpp"
#include "auth/Manager.hpp"
#include "auth/registration/Validator.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"
#include "identities/model/User.hpp"
#include "identities/model/Group.hpp"

#include <nlohmann/json.hpp>

using namespace vh::protocols::ws::handler;
using namespace vh::identities::model;

json Groups::add(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can create groups");

    const std::string groupName = payload.at("name").get<std::string>();
    const std::string groupDescription = payload.value("description", "");

    const auto group = std::make_shared<Group>();
    group->name = groupName;
    group->description = groupDescription;
    if (payload.contains("linux_gid")) {
        const auto linuxGid = payload.at("linux_gid").get<unsigned int>();
        if (linuxGid <= 0)
            throw std::runtime_error("Invalid Linux GID: must be a positive integer");
        group->linux_gid = linuxGid;
    }

    db::query::identities::Group::createGroup(group);

    return {{"name", groupName}};
}

json Groups::remove(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can delete groups");

    const auto groupId = payload.at("id").get<unsigned int>();
    db::query::identities::Group::deleteGroup(groupId);

    return {{"id", groupId}};
}

json Groups::addMember(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can add members to groups");

    const unsigned int groupId = payload.at("groupId").get<unsigned int>();
    const std::string memberName = payload.at("memberName").get<std::string>();

    const auto member = db::query::identities::User::getUserByName(memberName);
    if (!member) throw std::runtime_error("User not found: " + memberName);

    db::query::identities::Group::addMemberToGroup(groupId, member->id);

    return {{"groupId", groupId}, {"memberName", memberName}};
}

json Groups::removeMember(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can remove members from groups");

    const unsigned int groupId = payload.at("groupId").get<unsigned int>();
    const unsigned int userId = payload.at("userId").get<unsigned int>();

    db::query::identities::Group::removeMemberFromGroup(groupId, userId);

    return {{"groupId", groupId}, {"userId", userId}};
}

json Groups::list(const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can list groups");

    return {{"groups", db::query::identities::Group::listGroups()}};
}

json Groups::get(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can get group details");

    const auto groupId = payload.at("id").get<unsigned int>();
    auto group = db::query::identities::Group::getGroup(groupId);

    if (!group) throw std::runtime_error("Group not found");

    return {{"group", *group}};
}

json Groups::getByName(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can get group by name");

    if (const auto group = db::query::identities::Group::getGroupByName(payload.at("name").get<std::string>()))
        return {{"group", *group}};

    throw std::runtime_error("Group not found");
}


json Groups::update(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can update groups");

    const unsigned int groupId = payload.at("id").get<unsigned int>();
    const std::string newName = payload.at("name").get<std::string>();

    if (!auth::registration::Validator::isValidGroup(newName))
        throw std::runtime_error("Invalid group name: " + newName);

    const auto group = db::query::identities::Group::getGroup(groupId);
    if (!group) throw std::runtime_error("Group not found");

    group->name = newName;

    if (payload.contains("linux_gid")) {
        const auto linux_gid = payload.at("linux_gid").get<unsigned int>();
        if (linux_gid <= 0)
            throw std::runtime_error("Invalid Linux GID: must be a positive integer");
        group->linux_gid = linux_gid;
    }

    db::query::identities::Group::updateGroup(group);

    return {{"id", groupId}, {"name", newName}};
}

json Groups::listByUser(const json& payload, const std::shared_ptr<Session>& session) {
    if (const auto user = session->user; !user || !user->canManageRoles())
        throw std::runtime_error("Permission denied: Only admins can list groups by user");

    return {{"groups", db::query::identities::Group::listGroups(payload.at("user_id").get<unsigned int>())}};
}
