#pragma once

#include "rbac/Actor.hpp"
#include "rbac/fs/policy/Decision.hpp"
#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/role/Vault.hpp"
#include "share/Types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace vh::share {
struct Grant;
struct Principal;
struct ResolvedTarget;
}

namespace vh::rbac::fs::policy {

struct ShareRequest {
    uint32_t vault_id{};
    std::filesystem::path vault_path{};
    share::Operation operation{share::Operation::Metadata};
    share::TargetType target_type{share::TargetType::Directory};
    bool target_exists{true};
};

class Share {
public:
    [[nodiscard]]
    static role::Vault effectiveVaultRole(const share::Principal& principal, share::Operation operation);

    [[nodiscard]]
    static role::Vault effectiveVaultRole(const share::Principal& principal);

    [[nodiscard]]
    static role::Vault scopedVaultRoleForGrant(const std::string& shareId, const share::Grant& grant);

    [[nodiscard]]
    static std::optional<permission::vault::FilesystemAction> actionForOperation(share::Operation operation);

    [[nodiscard]]
    static Decision evaluate(const share::Principal& principal, const ShareRequest& request);

    [[nodiscard]]
    static Decision evaluate(const Actor& actor, const ShareRequest& request);

    [[nodiscard]]
    static Decision evaluate(const share::Principal& principal, const share::ResolvedTarget& target);

    [[nodiscard]]
    static Decision evaluate(const Actor& actor, const share::ResolvedTarget& target);
};

}
