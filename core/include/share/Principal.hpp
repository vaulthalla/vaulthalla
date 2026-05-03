#pragma once

#include "share/Grant.hpp"

#include <ctime>
#include <memory>
#include <optional>
#include <string>

namespace vh::rbac::role { struct Vault; }

namespace vh::share {

struct Principal {
    std::string share_id;
    std::string share_session_id;
    uint32_t vault_id{};
    uint32_t root_entry_id{};
    std::string root_path{"/"};
    Grant grant;
    bool email_verified{};
    std::time_t expires_at{};
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;
    std::shared_ptr<rbac::role::Vault> scoped_vault_role;

    [[nodiscard]] bool isExpired(std::time_t now) const;
    [[nodiscard]] bool isActive(std::time_t now) const;
    [[nodiscard]] bool allows(Operation op) const;
    [[nodiscard]] bool hasVault(uint32_t vaultId) const noexcept;
};

}
