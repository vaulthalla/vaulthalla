#include "CLITestContext.hpp"
#include "UsageManager.hpp"
#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "identities/Group.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "EntityType.hpp"
#include "generators.hpp"

#include <stdexcept>

using namespace vh::test::cli;
using namespace vh::protocols::shell;
using namespace vh::identities;
using namespace vh::rbac;
using namespace vh::vault::model;

CLITestContext::CLITestContext()
    : usage(std::make_shared<UsageManager>()) {
    for (const auto& entity : ENTITIES) {
        for (const auto& action : ACTIONS) {
            const auto eStr = std::string(entity);
            const auto aStr = std::string(action);
            if (entity == "role") {
                const auto cmdBase = eStr + " " + aStr;
                const auto userCmd = cmdBase + " user";
                const auto vaultCmd = cmdBase + " vault";
                const auto userUsage = usage->resolve({eStr, aStr});
                const auto vaultUsage = usage->resolve({eStr, aStr});
                if (!userUsage) throw std::runtime_error("EntityFactory: unknown command usage: " + userCmd);
                if (!vaultUsage) throw std::runtime_error("EntityFactory: unknown command usage: " + vaultCmd);
                commands[userCmd] = userUsage;
                commands[vaultCmd] = vaultUsage;
                continue;
            }
            const auto cmdName = eStr + " " + aStr;
            const auto cmdUsage = usage->resolve({eStr, aStr});
            if (!cmdUsage) throw std::runtime_error("EntityFactory: unknown command usage: " + cmdName);
            commands[cmdName] = cmdUsage;
        }
    }
}

std::string CLITestContext::getCommandName(const EntityType& type, const std::string& action) {
    switch (type) {
        case EntityType::USER:  return "user " + action;
        case EntityType::VAULT: return "vault " + action;
        case EntityType::GROUP: return "group " + action;
        case EntityType::USER_ROLE: return "role " + action + " user";
        case EntityType::VAULT_ROLE: return "role " + action + " vault";
        default: throw std::runtime_error("EntityFactory: unsupported entity type for command name");
    }
}

std::shared_ptr<CommandUsage> CLITestContext::getCommand(const EntityType& type, const std::string& action) const {
    return commands.at(getCommandName(type, action));
}

std::shared_ptr<User> CLITestContext::pickRandomUser() const {
    if (users.empty()) throw std::runtime_error("CLITestContext: no users available to pick from");
    return users[generateRandomIndex(users.size())];
}

std::shared_ptr<Group> CLITestContext::pickRandomGroup() const {
    if (groups.empty()) throw std::runtime_error("CLITestContext: no groups available to pick from");
    return groups[generateRandomIndex(groups.size())];
}

std::shared_ptr<Vault> CLITestContext::pickRandomVault() const {
    if (vaults.empty()) throw std::runtime_error("CLITestContext: no vaults available to pick from");
    return vaults[generateRandomIndex(vaults.size())];
}

std::shared_ptr<Vault> CLITestContext::pickVaultOwnedBy(const std::shared_ptr<User>& user) const {
    if (vaults.empty()) throw std::runtime_error("CLITestContext: no vaults available to pick from");
    std::vector<std::shared_ptr<Vault>> owned;
    for (const auto& v : vaults) if (v->owner_id == user->id) owned.push_back(v);
    if (owned.empty()) throw std::runtime_error("CLITestContext: user owns no vaults to pick from");
    return owned[generateRandomIndex(owned.size())];
}

std::shared_ptr<role::Admin> CLITestContext::randomUserRole() const {
    if (userRoles.empty()) throw std::runtime_error("CLITestContext: no user roles available to pick from");
    return userRoles[generateRandomIndex(userRoles.size())];
}

std::shared_ptr<role::Vault> CLITestContext::randomVaultRole() const {
    if (vaultRoles.empty()) throw std::runtime_error("CLITestContext: no vault roles available to pick from");
    return vaultRoles[generateRandomIndex(vaultRoles.size())];
}
