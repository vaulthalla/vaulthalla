#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <array>

namespace vh::identities { struct User; struct Group; }
namespace vh::rbac::role { struct Admin; struct Vault; }
namespace vh::vault::model { struct Vault; struct APIKey; }

namespace vh::protocols::shell {
class CommandUsage;
class UsageManager;
}

namespace vh::test::integration { enum class EntityType; }

namespace vh::test::integration::cli {

struct Context {
    static constexpr std::array<std::string_view, 4> ENTITIES = {"user", "vault", "group", "role"};
    static constexpr std::array<std::string_view, 5> ACTIONS = {"create", "update", "delete", "list", "info"};

    std::vector<std::shared_ptr<identities::User>> users;
    std::vector<std::shared_ptr<vault::model::APIKey>> api_keys;
    std::vector<std::shared_ptr<vault::model::Vault>> vaults;
    std::vector<std::shared_ptr<rbac::role::Admin>> adminRoles;
    std::vector<std::shared_ptr<rbac::role::Vault>> vaultRoles;
    std::vector<std::shared_ptr<identities::Group>> groups;
    std::shared_ptr<protocols::shell::UsageManager> usage;
    std::unordered_map<std::string, std::shared_ptr<protocols::shell::CommandUsage>> commands;

    Context();

    [[nodiscard]] std::shared_ptr<identities::User> pickRandomUser() const;
    [[nodiscard]] std::shared_ptr<identities::Group> pickRandomGroup() const;
    [[nodiscard]] std::shared_ptr<vault::model::Vault> pickRandomVault() const;
    [[nodiscard]] std::shared_ptr<vault::model::Vault> pickVaultOwnedBy(const std::shared_ptr<identities::User>& user) const;
    [[nodiscard]] std::shared_ptr<rbac::role::Admin> randomAdminRole() const;
    [[nodiscard]] std::shared_ptr<rbac::role::Vault> randomVaultRole() const;
    [[nodiscard]] static std::string getCommandName(const EntityType& type, const std::string& action);
    [[nodiscard]] std::shared_ptr<protocols::shell::CommandUsage> getCommand(const EntityType& type, const std::string& action) const;
};

}
