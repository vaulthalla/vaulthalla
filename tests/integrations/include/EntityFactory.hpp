#pragma once

#include "EntityType.hpp"
#include "generators.hpp"
#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "identities/Group.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "CLITestContext.hpp"
#include "permsUtil.hpp"

#include <string>
#include <memory>
#include <optional>

using namespace vh::identities;
using namespace vh::rbac;
using namespace vh::vault::model;

namespace vh::test::cli {

class EntityFactory {
public:
    explicit EntityFactory(const std::shared_ptr<CLITestContext>& ctx) : ctx_(ctx) {}

    [[nodiscard]] std::shared_ptr<void> create(const EntityType& type) const {
        if (type == EntityType::USER) {
            const std::string usage = "user/create";
            const auto user = std::make_shared<User>();
            user->name = generateName(usage);
            if (coin()) user->email = generateEmail(usage);
            user->roles.admin = ctx_->randomUserRole();
            return user;
        }

        if (type == EntityType::VAULT) {
            const std::string usage = "vault/create";
            const auto vault = std::make_shared<Vault>();
            vault->name = generateName(usage);
            vault->setQuotaFromStr(generateQuotaStr(usage));
            vault->owner_id = ctx_->pickRandomUser()->id;
            return vault;
        }

        if (type == EntityType::GROUP) {
            const auto group = std::make_shared<Group>();
            group->name = generateName("group/create");
            return group;
        }

        if (type == EntityType::USER_ROLE) {
            const auto role = std::make_shared<role::Admin>();
            role->name = generateRoleName(type, "role/create");
            role->description = "Auto-generated user role";
            // TODO: permissions
            // role->permissions = generateBitmask(ADMIN_SHELL_PERMS.size());
            return role;
        }

        if (type == EntityType::VAULT_ROLE) {
            const auto role = std::make_shared<Vault>();
            role->name = generateRoleName(type, "role/create");
            role->description = "Auto-generated vault role";
            // role->permissions = generateBitmask(VAULT_SHELL_PERMS.size());
            return role;
        }

        return nullptr;
    }

    void seedBaseline(const std::shared_ptr<CLITestContext>& ctx);

private:
    std::shared_ptr<CLITestContext> ctx_;
};

}
