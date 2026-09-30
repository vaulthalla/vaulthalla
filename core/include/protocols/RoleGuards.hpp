#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vh::identities { struct User; }
namespace vh::rbac::role {
    struct Admin;
    struct Vault;
}

// Role mutation rules shared by the CLI (shell/commands/rbac/roles/*) and the web (ws/handler/rbac/roles/*),
// so the two surfaces can't drift apart on who may create, change, or delete a role.
namespace vh::protocols::roles {

inline constexpr std::string_view kSuperAdminRoleName = "super_admin";

struct RoleAlreadyExists : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Insert-only creation. Never updates an existing role (by name or id); any id on the input is ignored.
// Throws RoleAlreadyExists when the name is taken, std::invalid_argument for an empty/reserved name.
[[nodiscard]] std::shared_ptr<rbac::role::Admin> createAdminRole(const std::shared_ptr<rbac::role::Admin>& role);
[[nodiscard]] std::shared_ptr<rbac::role::Vault> createVaultRole(const std::shared_ptr<rbac::role::Vault>& role);

// Why `caller` may not change (rename/re-permission) the existing admin role `existing` into `staged`, or
// nullopt when allowed. Covers the built-in super_admin role, renames onto the reserved name, and editing the
// role assigned to the caller's own account (self-escalation).
[[nodiscard]] std::optional<std::string> adminRoleUpdateError(const identities::User& caller,
                                                              const rbac::role::Admin& existing,
                                                              const rbac::role::Admin& staged);

// Why `caller` may not delete admin role `existing`, or nullopt when allowed (built-in super_admin, the
// caller's own role, or a role that still has assignments).
[[nodiscard]] std::optional<std::string> adminRoleDeleteError(const identities::User& caller,
                                                              const rbac::role::Admin& existing);

// Why vault role `existing` may not be deleted, or nullopt (still assigned somewhere).
[[nodiscard]] std::optional<std::string> vaultRoleDeleteError(const rbac::role::Vault& existing);

}
