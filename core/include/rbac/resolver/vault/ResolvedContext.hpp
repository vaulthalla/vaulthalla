#pragma once

#include "identities/User.hpp"
#include "identities/Group.hpp"
#include "storage/Engine.hpp"
#include "log/Registry.hpp"
#include "vault/model/Vault.hpp"

#include <memory>

namespace vh::rbac::resolver::vault {
    struct ResolvedContext {
        std::shared_ptr<storage::Engine> engine;
        std::shared_ptr<::vh::vault::model::Vault> vault;
        std::shared_ptr<identities::User> owner;
        std::shared_ptr<identities::User> targetUser;
        std::shared_ptr<identities::Group> targetGroup;

        [[nodiscard]] bool isValid() const {
            if (!engine) log::Registry::auth()->warn("ResolvedContext is missing engine");
            if (!vault) log::Registry::auth()->warn("ResolvedContext is missing vault");
            if (!owner) log::Registry::auth()->warn("ResolvedContext is missing owner");
            return engine && vault && owner;
        }

        [[nodiscard]] bool hasTargetUser() const { return !!targetUser; }
        [[nodiscard]] bool hasTargetGroup() const { return !!targetGroup; }
        [[nodiscard]] bool hasTargetSubject() const { return targetUser || targetGroup; }
    };
}
