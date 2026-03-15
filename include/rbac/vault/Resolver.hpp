#pragma once

#include "resolver/Context.hpp"
#include "resolver/AccessTraits/Fwd.hpp"

#include "identities/User.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "vault/model/Vault.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "db/query/identities/User.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace vh::rbac::vault {

class Resolver {
    struct ResolvedVaultContext {
        std::shared_ptr<storage::Engine> engine;
        std::shared_ptr<vh::vault::model::Vault> vault;
        std::shared_ptr<identities::User> owner;

        [[nodiscard]] bool isValid() const {
            return engine && vault && owner;
        }
    };

    template <typename EnumT>
    static bool checkPermission(const std::shared_ptr<identities::User>& user,
                                const ResolvedVaultContext& resolved,
                                const EnumT permission) {
        static_assert(std::is_enum_v<EnumT>, "vh::rbac::vault::Resolver::checkPermission(): EnumT must be an enum type");

        if (!user || !resolved.isValid()) return false;

        const auto vaultId = resolved.vault->id;

        if (user->hasDirectVaultRole(vaultId)) {
            const auto role = user->getDirectVaultRole(vaultId);
            if (!role) return false;

            if (AccessTraits<EnumT>::direct(*role).has(permission))
                return true;
        }

        const auto& globalPerms = user->globalVaultPerms();

        if (user->id == resolved.vault->owner_id)
            return AccessTraits<EnumT>::self(globalPerms.self).has(permission);

        if (resolved.owner->isAdmin())
            return AccessTraits<EnumT>::admin(globalPerms.admin).has(permission);

        return AccessTraits<EnumT>::user(globalPerms.user).has(permission);
    }

    static std::shared_ptr<storage::Engine> resolveEngine(const std::optional<uint32_t>& vaultId,
                                                          const std::optional<std::filesystem::path>& path) {
        auto& storageManager = runtime::Deps::get().storageManager;
        if (!storageManager) return nullptr;

        if (vaultId) return storageManager->getEngine(*vaultId);
        if (path && !path->empty()) return storageManager->resolveStorageEngine(*path);

        return nullptr;
    }

    static ResolvedVaultContext resolveVaultContext(const std::optional<uint32_t>& vaultId,
                                                    const std::optional<std::filesystem::path>& path) {
        ResolvedVaultContext resolved{};

        resolved.engine = resolveEngine(vaultId, path);
        if (!resolved.engine) return resolved;

        resolved.vault = resolved.engine->vault;
        if (!resolved.vault) return resolved;

        resolved.owner = db::query::identities::User::getUserById(resolved.vault->owner_id);
        return resolved;
    }

public:
    template<typename EnumT>
    static bool has(Context<EnumT>&& ctx) {
        if (!ctx.isValid()) return false;

        const auto& user = ctx.user;
        if (!user) return false;
        if (user->isSuperAdmin()) return true;

        const auto resolved = resolveVaultContext(ctx.vault_id, ctx.path);
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
