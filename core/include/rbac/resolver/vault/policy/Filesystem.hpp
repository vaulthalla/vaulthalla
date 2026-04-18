#pragma once

#include "rbac/fs/policy/Evaluator.hpp"
#include "rbac/resolver/vault/Context.hpp"
#include "rbac/resolver/vault/ResolvedContext.hpp"
#include "rbac/resolver/vault/policy/Base.hpp"
#include "log/Registry.hpp"

namespace vh::rbac::resolver::vault {
    template<>
    struct ContextPolicy<permission::vault::FilesystemAction> {
        static bool validate(
            const std::shared_ptr<identities::User> &actor,
            const ResolvedContext &resolved,
            const permission::vault::FilesystemAction action,
            const Context<permission::vault::FilesystemAction> &ctx
        ) {
            if (!actor) {
                log::Registry::auth()->warn("Access denied for unauthenticated user");
                return false;
            }

            if (!resolved.isValid()) return false;

            const fs::policy::Request req{
                .user = actor,
                .action = action,
                .vaultId = resolved.vault ? std::optional<uint32_t>{resolved.vault->id} : ctx.vault_id,
                .path = ctx.path,
                .entry = ctx.entry
            };

            const auto decision = fs::policy::Evaluator::evaluate(req);

            if (!decision.allowed)
                log::Registry::auth()->warn("Filesystem permission denied. Context:\n{}", decision.toString());

            return decision.allowed;
        }
    };
}
