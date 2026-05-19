#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace vh::identities { struct User; }
namespace vh::rbac::role { struct Admin; }

namespace vh::notifications {

struct SecurityAlertActor {
    std::string source;
    std::optional<std::uint32_t> userId;
    std::string userName;
};

[[nodiscard]] SecurityAlertActor actorFromUser(
    std::string source,
    const std::shared_ptr<identities::User>& user
);

void enqueueAdminRoleCreated(
    const std::shared_ptr<rbac::role::Admin>& role,
    const SecurityAlertActor& actor
);

void enqueueAdminRoleUpdated(
    const std::shared_ptr<rbac::role::Admin>& role,
    const SecurityAlertActor& actor
);

void enqueueAdminRoleDeleted(
    const std::shared_ptr<rbac::role::Admin>& role,
    const SecurityAlertActor& actor
);

}
