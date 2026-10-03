#pragma once

#include "CommandUsage.hpp"
#include "ops/Error.hpp"
#include "ops/Roles.hpp"
#include "protocols/shell/types.hpp"
#include "protocols/shell/util/usageOptions.hpp"
#include "rbac/resolver/permission/EnumPack.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace vh::protocols::shell {

// The command's --allow-<perm> / --deny-<perm> options as the one PermissionEdit both frontends use. Declared
// options (--desc, --from, --pattern, ...) are skipped; every other option must be a known permission flag for
// `RoleT` and takes no value (a value means a following word was swallowed, e.g. a misplaced positional).
template<class RoleT>
ops::roles::PermissionEdit permissionEditFromFlags(const CommandCall& call, const std::shared_ptr<CommandUsage>& usage) {
    using Resolver = typename rbac::resolver::PermissionResolverEnumPack<std::shared_ptr<RoleT>>::type;
    const auto exported = std::make_shared<RoleT>()->toPermissions();
    const auto byFlag = Resolver::buildFlagMap(exported);

    ops::roles::PermissionEdit edit;
    for (const auto& opt : call.options) {
        if (usage && usageDeclaresOption(*usage, opt.key)) continue;
        const auto flag = "--" + opt.key;
        const auto it = byFlag.find(flag);
        if (it == byFlag.end()) throw ops::Invalid("unknown permission flag '" + flag + "' (see 'vh permission')");
        if (opt.value) throw ops::Invalid("permission flag '" + flag + "' takes no value (got '" + *opt.value + "')");
        edit.changes.emplace_back(it->second.permission->qualified_name,
                                  it->second.operation == rbac::resolver::PermissionOperation::Grant);
    }
    return edit;
}

}
