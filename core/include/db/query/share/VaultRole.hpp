#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace vh::rbac::role { struct Vault; }

namespace vh::db::query::share {

struct VaultRole {
    static uint32_t upsertForShare(
        const std::string& shareId,
        uint32_t vaultId,
        const std::shared_ptr<vh::rbac::role::Vault>& role
    );

    static std::shared_ptr<vh::rbac::role::Vault> getForShare(const std::string& shareId);

    static void removeForShare(const std::string& shareId);
};

}
