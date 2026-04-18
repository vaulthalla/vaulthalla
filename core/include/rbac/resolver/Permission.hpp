#pragma once

#include "rbac/permission/Permission.hpp"
#include "rbac/resolver/permission/Fwd.hpp"
#include "rbac/resolver/permission/helpers.hpp"
#include "rbac/resolver/permission/TargetTraits.hpp"
#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/fs/glob/Tokenizer.hpp"
#include "rbac/fs/glob/Matcher.hpp"

#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <algorithm>
#include <optional>

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

                    if (auto [it, inserted] = byFlag.emplace(flag, PermissionLookup{&perm, op}); !inserted)
                        throw std::runtime_error("Duplicate permission flag mapping: " + flag);
                }

            return byFlag;
        }

        static void applyPermissionsFromWebCli(
            Role &role,
            const std::vector<permission::Permission> &permissions,
            const std::unordered_map<std::string, bool> &newVals
        ) {
            for (const auto &perm: permissions) {
                if (!newVals.contains(perm.qualified_name))
                    throw std::runtime_error("Missing permission value for: " + perm.qualified_name);

                apply(
                    role,
                    perm,
                    newVals.at(perm.qualified_name)
                        ? PermissionOperation::Grant
                        : PermissionOperation::Revoke
                );
            }
        }

        static bool apply(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            return (dispatchApplyOne<Enums>(role, perm, op) || ...);
        }

        static bool apply(Role &role, const PermissionLookup &lookup) {
            if (!lookup.permission) return false;
            return apply(role, *lookup.permission, lookup.operation);
        }

        static bool applyOverride(
            Role &role,
            const PermissionLookup &lookup,
            const fs::glob::model::Pattern &pattern,
            const bool enabled = true
        ) {
            if (!lookup.permission) return false;
            if (!hasOverrideSupport(*lookup.permission)) return false;
            if (!fs::glob::Tokenizer::isValid(pattern.source)) return false;

            auto &overrideSet = overrides(role);
            auto &overrideOwner = requireOverrideRole(role);

            permission::Override ov;
            ov.assignment_id = overrideOwner.assignment_id;
            ov.permission = *lookup.permission;
            ov.pattern = pattern;
            ov.effect = overrideOptFromPermissionOp(lookup.operation);
            ov.enabled = enabled;

            overrideSet.push_back(ov);
            return true;
        }

        static bool updateOverride(
            Role &role,
            const PermissionLookup &lookup,
            const std::string &target,
            const bool enabled = true
        ) {
            if (!lookup.permission) return false;
            if (!hasOverrideSupport(*lookup.permission)) return false;

            auto &overrideSet = overrides(role);

            for (auto &ov: overrideSet)
                if (fs::glob::Matcher::matches(ov.pattern, target) && ov.permission.id == lookup.permission->id) {
                    handleUpdateOverride(lookup, ov, enabled);
                    return true;
                }

            return false;
        }

        static bool updateOverride(
            Role &role,
            const PermissionLookup &lookup,
            const uint32_t target,
            const bool enabled = true
        ) {
            if (!lookup.permission) return false;
            if (!hasOverrideSupport(*lookup.permission)) return false;

            auto &overrideSet = overrides(role);

            for (auto &ov: overrideSet)
                if (ov.id == target && ov.permission.id == lookup.permission->id) {
                    handleUpdateOverride(lookup, ov, enabled);
                    return true;
                }

            return false;
        }

        static bool removeOverride(Role &role, const uint32_t target) {
            auto &overrideSet = overrides(role);

            auto it = std::remove_if(
                overrideSet.begin(),
                overrideSet.end(),
                [&](const permission::Override &ov) {
                    return ov.id == target;
                }
            );

            if (it != overrideSet.end()) {
                overrideSet.erase(it, overrideSet.end());
                return true;
            }

            return false;
        }

        static std::optional<permission::Override> findOverride(
            const Role &role,
            const permission::Permission &perm,
            const std::string &target
        ) {
            if (!hasOverrideSupport(perm)) return std::nullopt;

            const auto &overrideSet = overrides(role);
            for (const auto &ov: overrideSet)
                if (fs::glob::Matcher::matches(ov.pattern, target) && ov.permission.id == perm.id)
                    return ov;

            return std::nullopt;
        }

        static std::optional<permission::Override> findOverride(
            const Role &role,
            const permission::Permission &perm,
            const uint32_t target
        ) {
            if (!hasOverrideSupport(perm)) return std::nullopt;

            const auto &overrideSet = overrides(role);
            for (const auto &ov: overrideSet)
                if (ov.id == target && ov.permission.id == perm.id)
                    return ov;

            return std::nullopt;
        }

        static bool has(const Role &role, const permission::Permission &perm) {
            return (dispatchHasOne<Enums>(role, perm) || ...);
        }

        static bool hasOverrideSupport(const permission::Permission &perm) {
            return (dispatchHasOverrideSupportOne<Enums>(perm) || ...);
        }

        static auto &overrides(Role &role) {
            auto &r = requireOverrideRole(role);
            return r.fs.overrides;
        }

        static const auto &overrides(const Role &role) {
            const auto &r = requireOverrideRole(role);
            return r.fs.overrides;
        }

    private:
        template<typename T>
        using RawT = std::remove_cvref_t<T>;

        template<typename T>
        static constexpr bool IsSharedPtr = false;

        template<typename T>
        static constexpr bool IsSharedPtr<std::shared_ptr<T>> = true;

        template<typename T>
        using RoleObjectT = std::conditional_t<
            IsSharedPtr<RawT<T>>,
            typename RawT<T>::element_type,
            RawT<T>
        >;

        template<typename T>
        static constexpr bool IsSupportedPermissionRole =
            std::same_as<RawT<T>, role::Vault> ||
            std::same_as<RawT<T>, role::vault::Global>;

        template<typename T>
        static constexpr bool HasFsOverrides = requires(T t) {
            t.fs.overrides;
        };

        static auto &roleObject(Role &role) {
            if constexpr (IsSharedPtr<Role>) {
                if (!role) throw std::runtime_error("Permission resolver received null role");
                return *role;
            } else {
                return role;
            }
        }

        static const auto &roleObject(const Role &role) {
            if constexpr (IsSharedPtr<Role>) {
                if (!role) throw std::runtime_error("Permission resolver received null role");
                return *role;
            } else {
                return role;
            }
        }

        static auto &requirePermissionRole(Role &role) {
            if constexpr (IsSharedPtr<Role>) {
                if (!role) throw std::runtime_error("Permission resolver requires a non-null role");

                using Pointee = RoleObjectT<Role>;

                if constexpr (std::same_as<Pointee, role::Vault> || std::same_as<Pointee, role::vault::Global>) {
                    return *role;
                } else if (auto vault = std::dynamic_pointer_cast<role::Vault>(role); vault) {
                    return *vault;
                } else if (auto global = std::dynamic_pointer_cast<role::vault::Global>(role); global) {
                    return *global;
                } else {
                    throw std::runtime_error("Permission resolver requires a supported role type");
                }
            } else {
                using Obj = RawT<Role>;

                if constexpr (IsSupportedPermissionRole<Obj>) {
                    return role;
                } else {
                    throw std::runtime_error("Permission resolver requires a supported role type");
                }
            }
        }

        static const auto &requirePermissionRole(const Role &role) {
            if constexpr (IsSharedPtr<Role>) {
                if (!role) throw std::runtime_error("Permission resolver requires a non-null role");

                using Pointee = RoleObjectT<Role>;

                if constexpr (std::same_as<Pointee, role::Vault> || std::same_as<Pointee, role::vault::Global>) {
                    return *role;
                } else if (auto vault = std::dynamic_pointer_cast<const role::Vault>(role); vault) {
                    return *vault;
                } else if (auto global = std::dynamic_pointer_cast<const role::vault::Global>(role); global) {
                    return *global;
                } else {
                    throw std::runtime_error("Permission resolver requires a supported role type");
                }
            } else {
                using Obj = RawT<Role>;

                if constexpr (IsSupportedPermissionRole<Obj>) {
                    return role;
                } else {
                    throw std::runtime_error("Permission resolver requires a supported role type");
                }
            }
        }

        static auto &requireOverrideRole(Role &role) {
            auto &r = requirePermissionRole(role);
            using Obj = std::remove_cvref_t<decltype(r)>;

            if constexpr (HasFsOverrides<Obj>) {
                return r;
            } else {
                throw std::runtime_error("Permission overrides are not supported for this role type");
            }
        }

        static const auto &requireOverrideRole(const Role &role) {
            const auto &r = requirePermissionRole(role);
            using Obj = std::remove_cvref_t<decltype(r)>;

            if constexpr (HasFsOverrides<Obj>) {
                return r;
            } else {
                throw std::runtime_error("Permission overrides are not supported for this role type");
            }
        }

        static bool handleUpdateOverride(
            const PermissionLookup &lookup,
            permission::Override &ov,
            const bool enabled = true
        ) {
            if (!hasOverrideSupport(*lookup.permission)) return false;

            ov.effect = overrideOptFromPermissionOp(lookup.operation);
            ov.enabled = enabled;

            return true;
        }

        template<typename Enum>
        static bool tryApplyDirect(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            using Traits = PermissionTargetTraits<Enum>;
            using RawRole = std::remove_cvref_t<decltype(roleObject(role))>;

            if constexpr (!HasMutableTarget<Traits, RawRole>)
                return false;
            else {
                auto &target = Traits::target(roleObject(role));
                return applyToSet<Enum>(target, perm, op);
            }
        }

        template<typename Enum>
        static bool tryHasDirect(const Role &role, const permission::Permission &perm) {
            using Traits = PermissionTargetTraits<Enum>;
            using RawRole = std::remove_cvref_t<decltype(roleObject(role))>;

            if constexpr (!HasConstTarget<Traits, RawRole>)
                return false;
            else {
                const auto &target = Traits::target(roleObject(role));
                return hasInSet<Enum>(target, perm);
            }
        }

        template<typename Enum>
        static bool tryApplyContext(Role &role, const permission::Permission &perm, const PermissionOperation op) {
            using Traits = PermissionContextPolicyTraits<Enum>;
            using RawRole = std::remove_cvref_t<decltype(roleObject(role))>;

            if constexpr (!HasMutableResolve<Traits, RawRole>)
                return false;
            else {
                const auto parts = splitQualifiedName(perm.qualified_name);
                auto *target = Traits::resolve(roleObject(role), parts);
                if (!target) return false;
                return applyToSet<Enum>(*target, perm, op);
            }
        }

        template<typename Enum>
        static bool tryHasContext(const Role &role, const permission::Permission &perm) {
            using Traits = PermissionContextPolicyTraits<Enum>;
            using RawRole = std::remove_cvref_t<decltype(roleObject(role))>;

            if constexpr (!HasConstResolve<Traits, RawRole>)
                return false;
            else {
                const auto parts = splitQualifiedName(perm.qualified_name);
                const auto *target = Traits::resolve(roleObject(role), parts);
                if (!target) return false;
                return hasInSet<Enum>(*target, perm);
            }
        }

        template<typename Enum>
        static bool tryHasOverrideSupportDirect(const permission::Permission &perm) {
            using Traits = PermissionTargetTraits<Enum>;
            if (perm.enumType != typeid(Enum)) return false;
            return Traits::canOverride;
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

        template<typename Enum>
        static bool dispatchHasOverrideSupportOne(const permission::Permission &perm) {
            return tryHasOverrideSupportDirect<Enum>(perm);
        }
    };
}
