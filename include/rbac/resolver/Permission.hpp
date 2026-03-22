#pragma once

#include "rbac/permission/Permission.hpp"
#include "rbac/resolver/permission/Fwd.hpp"
#include "rbac/resolver/permission/helpers.hpp"
#include "rbac/resolver/permission/TargetTraits.hpp"

#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace vh::rbac::resolver {
    struct PermissionLookup {
        const permission::Permission *permission{};
        PermissionOperation operation{};
    };

    template<typename RoleT, typename... Enums>
    class PermissionResolver {
    public:
        using Role = RoleT;

        static std::unordered_map<std::string, PermissionLookup>
        buildFlagMap(const std::vector<permission::Permission> &permissions) {
            std::unordered_map<std::string, PermissionLookup> byFlag;

            for (const auto &perm: permissions)
                for (const auto &flag: perm.flags) {
                    const auto op = flag.starts_with("--deny-")
                                        ? PermissionOperation::Revoke
                                        : PermissionOperation::Grant;

                    auto [it, inserted] = byFlag.emplace(flag, PermissionLookup{&perm, op});
                    if (!inserted)
                        throw std::runtime_error("Duplicate permission flag mapping: " + flag);
                }

            return byFlag;
        }

        static bool apply(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            return (dispatchApplyOne<Enums>(role, perm, op) || ...);
        }

        static bool has(const Role &role, const permission::Permission &perm) {
            return (dispatchHasOne<Enums>(role, perm) || ...);
        }

    private:
        template<typename Enum>
        static bool tryApplyDirect(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            using Traits = PermissionTargetTraits<Enum>;

            if constexpr (!HasMutableTarget<Traits, Role>)
                return false;
            else {
                auto &target = Traits::target(role);
                return applyToSet<Enum>(target, perm, op);
            }
        }

        template<typename Enum>
        static bool tryHasDirect(const Role &role, const permission::Permission &perm) {
            using Traits = PermissionTargetTraits<Enum>;

            if constexpr (!HasConstTarget<Traits, Role>)
                return false;
            else {
                const auto &target = Traits::target(role);
                return hasInSet<Enum>(target, perm);
            }
        }

        template<typename Enum>
        static bool tryApplyContext(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            using Traits = PermissionContextPolicyTraits<Enum>;

            if constexpr (!HasMutableResolve<Traits, Role>)
                return false;
            else {
                const auto parts = splitQualifiedName(perm.name);
                auto *target = Traits::resolve(role, parts);
                if (!target) return false;
                return applyToSet<Enum>(*target, perm, op);
            }
        }

        template<typename Enum>
        static bool tryHasContext(const Role &role, const permission::Permission &perm) {
            using Traits = PermissionContextPolicyTraits<Enum>;

            if constexpr (!HasConstResolve<Traits, Role>)
                return false;
            else {
                const auto parts = splitQualifiedName(perm.name);
                const auto *target = Traits::resolve(role, parts);
                if (!target) return false;
                return hasInSet<Enum>(*target, perm);
            }
        }

        template<typename Enum>
        static bool dispatchApplyOne(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            if (perm.enumType != typeid(Enum)) return false;
            if (tryApplyDirect<Enum>(role, perm, op)) return true;
            if (tryApplyContext<Enum>(role, perm, op)) return true;
            return false;
        }

        template<typename Enum>
        static bool dispatchHasOne(const Role &role, const permission::Permission &perm) {
            if (perm.enumType != typeid(Enum)) return false;
            if (tryHasDirect<Enum>(role, perm)) return true;
            if (tryHasContext<Enum>(role, perm)) return true;
            return false;
        }
    };
}
