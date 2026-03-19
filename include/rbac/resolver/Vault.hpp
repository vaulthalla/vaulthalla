#pragma once

#include "vault/Context.hpp"
#include "vault/ResolvedContext.hpp"
#include "ResolverTraits/Fwd.hpp"
#include "vault/policy/Base.hpp"
#include "vault/ResolverMode.hpp"

#include "identities/User.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/admin/VaultGlobals.hpp"
#include "vault/model/Vault.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace vh::rbac::resolver {
    class Vault {
        template<typename EnumT>
        static std::vector<EnumT> collectPermissions(const vault::Context<EnumT> &ctx) {
            static_assert(std::is_enum_v<EnumT>,
                          "vh::rbac::resolver::Vault::collectPermissions(): EnumT must be an enum type");

            std::vector<EnumT> out;
            out.reserve((ctx.permission.has_value() ? 1u : 0u) + ctx.permissions.size());

            if (ctx.permission) out.push_back(*ctx.permission);
            out.insert(out.end(), ctx.permissions.begin(), ctx.permissions.end());

            if (out.empty())
                throw std::invalid_argument(
                    "vh::rbac::resolver::Vault::has(): Context must provide either permission or permissions");

            return out;
        }

        template<typename EnumT>
        static bool checkSinglePermission(
            const std::shared_ptr<identities::User> &user,
            const vault::ResolvedContext &resolved,
            const vault::Context<EnumT> &ctx,
            const EnumT permission
        ) {
            static_assert(std::is_enum_v<EnumT>,
                          "vh::rbac::resolver::Vault::checkSinglePermission(): EnumT must be an enum type");

            if (!user || !resolved.isValid()) return false;

            if constexpr (VaultResolverMode<EnumT>::policy_only)
                return vault::ContextPolicy<EnumT>::validate(user, resolved, permission, ctx);

            bool allowed = false;
            const auto vaultId = resolved.vault->id;

            if (user->hasDirectVaultRole(vaultId)) {
                const auto role = user->getDirectVaultRole(vaultId);
                if (!role) return false;

                if (VaultResolverTraits<EnumT>::direct(*role).has(permission))
                    allowed = true;
            }

            if (!allowed) {
                const auto &globalPerms = user->vaultGlobals();

                if (user->id == resolved.vault->owner_id)
                    allowed = VaultResolverTraits<EnumT>::self(globalPerms.self).has(permission);
                else if (resolved.owner->isAdmin())
                    allowed = VaultResolverTraits<EnumT>::admin(globalPerms.admin).has(permission);
                else
                    allowed = VaultResolverTraits<EnumT>::user(globalPerms.user).has(permission);
            }

            if (!allowed) return false;

            return vault::ContextPolicy<EnumT>::validate(user, resolved, permission, ctx);
        }

        template<typename EnumT>
        static bool checkPermissions(
            const std::shared_ptr<identities::User> &user,
            const vault::ResolvedContext &resolved,
            const vault::Context<EnumT> &ctx
        ) {
            const auto permissions = collectPermissions(ctx);

            for (const auto permission : permissions)
                if (!checkSinglePermission(user, resolved, ctx, permission))
                    return false;

            return true;
        }

        static std::shared_ptr<storage::Engine> resolveEngine(
            const std::optional<uint32_t> &vaultId,
            const std::optional<std::filesystem::path> &path
        ) {
            auto &storageManager = runtime::Deps::get().storageManager;
            if (!storageManager) return nullptr;

            if (vaultId) return storageManager->getEngine(*vaultId);
            if (path && !path->empty()) return storageManager->resolveStorageEngine(*path);

            return nullptr;
        }

        static vault::ResolvedContext resolveVaultContext(
            const std::optional<uint32_t> &vaultId,
            const std::optional<std::filesystem::path> &path,
            const std::optional<std::string> &targetSubjectType,
            const std::optional<uint32_t> &targetSubjectId
        ) {
            vault::ResolvedContext resolved{};

            resolved.engine = resolveEngine(vaultId, path);
            if (!resolved.engine) return resolved;

            resolved.vault = resolved.engine->vault;
            if (!resolved.vault) return resolved;

            resolved.owner = db::query::identities::User::getUserById(resolved.vault->owner_id);
            if (!resolved.owner) return resolved;

            if (targetSubjectType && targetSubjectId) {
                if (*targetSubjectType == "user")
                    resolved.targetUser = db::query::identities::User::getUserById(*targetSubjectId);
                else if (*targetSubjectType == "group")
                    resolved.targetGroup = db::query::identities::Group::getGroup(*targetSubjectId);
            }

            return resolved;
        }

    public:
        template<typename EnumT>
        static bool has(vault::Context<EnumT> &&ctx) {
            if (!ctx.isValid()) return false;

            const auto &user = ctx.user;
            if (!user) return false;
            if (user->isSuperAdmin()) return true;

            const auto resolved = resolveVaultContext(
                ctx.vault_id,
                ctx.path,
                ctx.target_subject_type,
                ctx.target_subject_id
            );

            if (!resolved.isValid()) return false;

            return checkPermissions(user, resolved, ctx);
        }

        template<typename EnumT>
        static bool has(const vault::Context<EnumT> &ctx) {
            return has(vault::Context<EnumT>{ctx});
        }

        template<typename... ContextTs>
        static bool hasAll(ContextTs &&... ctxs) {
            return (has(std::forward<ContextTs>(ctxs)) && ...);
        }

        template<typename... ContextTs>
        static bool hasAny(ContextTs &&... ctxs) {
            return (has(std::forward<ContextTs>(ctxs)) || ...);
        }
    };
}
