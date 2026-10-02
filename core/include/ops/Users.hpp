#pragma once

#include "ops/Actor.hpp"
#include "db/model/ListQueryParams.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::rbac::role { struct Admin; }

// User accounts, shared by `vh user ...` and the ws auth.register / auth.user.* commands.
//
// Who may manage an account depends on what the account can do: one whose admin role grants anything beyond the
// self scopes is an admin identity, governed by admin.identities.admins; anyone else is governed by
// admin.identities.users. On top of that, the escalation ceiling: nobody assigns a role that grants admin
// permissions they lack, and nobody changes, deletes or resets the password of an account whose role does.
// super_admin is never assigned, renamed or removed here, and protected accounts are not managed here at all.
namespace vh::ops::users {

using UserPtr = std::shared_ptr<identities::User>;

struct Create {
    std::string name;
    std::string role;                          // admin role name or id
    std::optional<std::string> email{};
    std::optional<std::string> password{};     // absent: generated and returned once
    bool is_active{true};
    std::optional<uint32_t> linux_uid{};
};

struct Created {
    UserPtr user;
    std::optional<std::string> generated_password{};
};

// Absent fields stay as they are.
struct Update {
    uint32_t id{};
    std::optional<std::string> name{};
    std::optional<std::optional<std::string>> email{};   // {{}}: clear it
    std::optional<std::string> role{};
    std::optional<bool> is_active{};
    std::optional<uint32_t> linux_uid{};
};

// True when the role grants any admin permission outside the self scopes.
[[nodiscard]] bool isAdminIdentity(const rbac::role::Admin& role);

// Creates the account and its default vault.
[[nodiscard]] Created create(const Actor& actor, const Create& req);
// Role changes and deactivation end the account's sessions.
UserPtr update(const Actor& actor, const Update& req);
// Ends the account's sessions, then removes it. Returns what was removed.
UserPtr remove(const Actor& actor, uint32_t id);
[[nodiscard]] UserPtr get(const Actor& actor, uint32_t id);
[[nodiscard]] UserPtr getByName(const Actor& actor, const std::string& name);
// The accounts the actor may view.
[[nodiscard]] std::vector<UserPtr> list(const Actor& actor, db::model::ListQueryParams params = {});
// Your own password needs the current one; anyone else's needs reset-password over them (and ends their sessions).
UserPtr changePassword(const Actor& actor, uint32_t id, const std::optional<std::string>& currentPassword,
                       const std::string& newPassword);

}
