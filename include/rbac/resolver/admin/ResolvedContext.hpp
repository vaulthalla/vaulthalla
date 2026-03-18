#pragma once

#include "identities/User.hpp"
#include "identities/Group.hpp"
#include "storage/Engine.hpp"
#include "rbac/resolver/admin/Context.hpp"

#include <memory>

namespace vh::rbac::resolver::admin {

    struct ResolvedContext {
        std::shared_ptr<storage::Engine> engine;
        std::shared_ptr<vh::vault::model::Vault> vault;
        std::shared_ptr<identities::User> owner;
        std::shared_ptr<identities::Group> group;
        std::optional<Entity> identity{};  // Users, Admins, Groups

        [[nodiscard]] bool isValid() const {
            if (identity == Entity::Group) return !!group;
            return !!owner;
        }

        [[nodiscard]] bool hasOwner() const { return !!owner; }
    };

}
