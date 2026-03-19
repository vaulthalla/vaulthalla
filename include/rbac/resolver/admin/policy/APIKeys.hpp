#pragma once

#include "Base.hpp"
#include "rbac/resolver/admin/ResolvedContext.hpp"
#include "rbac/permission/admin/Keys.hpp"
#include "identities/User.hpp"

#include <functional>
#include <memory>

namespace vh::rbac::resolver::admin {

template <>
struct ContextPolicy<permission::admin::keys::APIPermissions> {
private:
    using Perm = permission::admin::keys::APIPermissions;
    using User = identities::User;

    enum class TargetClass {
        Invalid,
        Self,
        Admin,
        User
    };

    static TargetClass classifyTarget(const std::shared_ptr<User>& actor,
                                      const ResolvedContext& resolved) {
        if (!actor || !resolved.isValid() || !resolved.owner)
            return TargetClass::Invalid;

        const auto& owner = resolved.owner;

        if (actor->id == owner->id)
            return TargetClass::Self;

        if (owner->isSuperAdmin())
            return TargetClass::Invalid;

        if (owner->isAdmin())
            return TargetClass::Admin;

        return TargetClass::User;
    }

    template <typename Predicate>
    static bool validateForTarget(const std::shared_ptr<User>& actor,
                                  const ResolvedContext& resolved,
                                  Predicate&& allowed) {
        switch (classifyTarget(actor, resolved)) {
            case TargetClass::Self:
                return allowed(actor->apiKeysPerms().self);

            case TargetClass::Admin:
                return allowed(actor->apiKeysPerms().admin);

            case TargetClass::User:
                return allowed(actor->apiKeysPerms().user);

            case TargetClass::Invalid:
            default:
                return false;
        }
    }

    static bool validateView(const std::shared_ptr<User>& actor,
                             const ResolvedContext& resolved) {
        return validateForTarget(actor, resolved, [](const auto& perms) {
            return perms.canView();
        });
    }

    static bool validateCreate(const std::shared_ptr<User>& actor,
                               const ResolvedContext& resolved) {
        return validateForTarget(actor, resolved, [](const auto& perms) {
            return perms.canCreate();
        });
    }

    static bool validateEdit(const std::shared_ptr<User>& actor,
                             const ResolvedContext& resolved) {
        return validateForTarget(actor, resolved, [](const auto& perms) {
            return perms.canEdit();
        });
    }

    static bool validateRemove(const std::shared_ptr<User>& actor,
                               const ResolvedContext& resolved) {
        return validateForTarget(actor, resolved, [](const auto& perms) {
            return perms.canRemove();
        });
    }

    static bool validateExport(const std::shared_ptr<User>& actor,
                               const ResolvedContext& resolved) {
        return validateForTarget(actor, resolved, [](const auto& perms) {
            return perms.canExport();
        });
    }

    static bool validateRotate(const std::shared_ptr<User>& actor,
                               const ResolvedContext& resolved) {
        return validateForTarget(actor, resolved, [](const auto& perms) {
            return perms.canRotate();
        });
    }

public:
    static bool validate(const std::shared_ptr<User>& actor,
                         const ResolvedContext& resolved,
                         const Perm permission,
                         const Context<permission::admin::keys::APIPermissions> &ctx) {
        (void)ctx;
        switch (permission) {
            case Perm::View:   return validateView(actor, resolved);
            case Perm::Create: return validateCreate(actor, resolved);
            case Perm::Edit:   return validateEdit(actor, resolved);
            case Perm::Remove: return validateRemove(actor, resolved);
            case Perm::Export: return validateExport(actor, resolved);
            case Perm::Rotate: return validateRotate(actor, resolved);

            default:
                return false;
        }
    }
};

}
