#pragma once

#include "rbac/fs/policy/Evaluator.hpp"
#include "rbac/resolver/vault/Context.hpp"
#include "rbac/resolver/vault/ResolvedContext.hpp"
#include "rbac/resolver/vault/policy/Base.hpp"

namespace vh::rbac::resolver::vault {
    template<>
    struct ContextPolicy<permission::vault::FilesystemAction> {
        static bool validate(
            const std::shared_ptr<identities::User> &actor,
            const ResolvedContext &resolved,
            permission::vault::FilesystemAction action,
            const Context<permission::vault::FilesystemAction> &ctx
        ) {
            if (!actor || !resolved.isValid()) return false;

            fs::policy::Request req{
                .user = actor,
                .action = action,
                .path = ctx.path,
                .entry = {}
            };

            const auto decision = fs::policy::Evaluator::evaluate(req);

            // Optional:
            // stash decision somewhere for later error/report handling

            return decision.allowed;
        }
    };
}
