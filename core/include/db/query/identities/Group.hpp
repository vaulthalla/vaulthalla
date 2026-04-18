#pragma once

#include "db/model/ListQueryParams.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace vh::identities { struct Group; }

namespace vh::db::query::identities {

class Group {
    using G = vh::identities::Group;
    using GroupPtr = std::shared_ptr<G>;

public:
    Group() = default;

    static uint32_t createGroup(const GroupPtr& group);
    static void updateGroup(const GroupPtr& group);
    static void deleteGroup(uint32_t groupId);
    static void addMemberToGroup(uint32_t group, uint32_t member);
    static void addMembersToGroup(uint32_t group, const std::vector<uint32_t>& members);
    static void removeMemberFromGroup(uint32_t group, uint32_t member);
    static void removeMembersFromGroup(uint32_t group, const std::vector<uint32_t>& members);
    static GroupPtr getGroup(uint32_t groupId);
    static GroupPtr getGroupByName(const std::string& name);
    static std::vector<GroupPtr> listGroups(const std::optional<uint32_t>& userId = {}, model::ListQueryParams&& params = {});
    [[nodiscard]] static bool groupExists(const std::string& name);
    static GroupPtr getGroupByLinuxGID(uint32_t gid);
};

}
