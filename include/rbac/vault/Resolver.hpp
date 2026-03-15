#pragma once

#include "resolver/Context.hpp"
#include "resolver/AccessTraits/Fwd.hpp"
#include "resolver/ContextPolicy/Base.hpp"
#include "resolver/VaultContext.hpp"

#include "identities/User.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "vault/model/Vault.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/identities/Group.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace vh::rbac::vault {

class Resolver {
    template <typename EnumT>
    static bool checkPermission(const std::shared_ptr<identities::User>& user,
                                const resolver::ResolvedVaultContext& resolved,
                                const EnumT permission) {
        static_assert(std::is_enum_v<EnumT>,
                      "vh::rbac::vault::Resolver::checkPermission(): EnumT must be an enum type");

        if (!user || !resolved.isValid()) return false;

        bool allowed = false;
        const auto vaultId = resolved.vault->id;

        if (user->hasDirectVaultRole(vaultId)) {
            const auto role = user->getDirectVaultRole(vaultId);
            if (!role) return false;

            if (AccessTraits<EnumT>::direct(*role).has(permission))
                allowed = true;
        }

        if (!allowed) {
            const auto& globalPerms = user->globalVaultPerms();

            if (user->id == resolved.vault->owner_id)
                allowed = AccessTraits<EnumT>::self(globalPerms.self).has(permission);
            else if (resolved.owner->isAdmin())
                allowed = AccessTraits<EnumT>::admin(globalPerms.admin).has(permission);
            else
                allowed = AccessTraits<EnumT>::user(globalPerms.user).has(permission);
        }

        if (!allowed) return false;

        return resolver::ContextPolicy<EnumT>::validate(user, resolved, permission);
    }

    static std::shared_ptr<storage::Engine> resolveEngine(const std::optional<uint32_t>& vaultId,
                                                          const std::optional<std::filesystem::path>& path) {
        auto& storageManager = runtime::Deps::get().storageManager;
        if (!storageManager) return nullptr;

        if (vaultId) return storageManager->getEngine(*vaultId);
        if (path && !path->empty()) return storageManager->resolveStorageEngine(*path);

        return nullptr;
    }

    static resolver::ResolvedVaultContext resolveVaultContext(const std::optional<uint32_t>& vaultId,
                                                              const std::optional<std::filesystem::path>& path,
                                                              const std::optional<std::string>& targetSubjectType,
                                                              const std::optional<uint32_t>& targetSubjectId) {
        resolver::ResolvedVaultContext resolved{};

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
    static bool has(Context<EnumT>&& ctx) {
        if (!ctx.isValid()) return false;

        const auto& user = ctx.user;
        if (!user) return false;
        if (user->isSuperAdmin()) return true;

        const auto resolved = resolveVaultContext(
            ctx.vault_id,
            ctx.path,
            ctx.target_subject_type,
            ctx.target_subject_id
        );

        if (!resolved.isValid()) return false;

        return checkPermission(user, resolved, ctx.permission);
    }

    template<typename EnumT>
    static bool has(const Context<EnumT>& ctx) {
        return has(Context<EnumT>{ctx});
    }

    template<typename... ContextTs>
    static bool hasAll(ContextTs&&... ctxs) {
        return (has(std::forward<ContextTs>(ctxs)) && ...);
    }

    template<typename... ContextTs>
    static bool hasAny(ContextTs&&... ctxs) {
        return (has(std::forward<ContextTs>(ctxs)) || ...);
    }
};

}
