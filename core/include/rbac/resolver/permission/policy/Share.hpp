#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/permission/vault/fs/Share.hpp"

#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    // vault.fs.{files,directories}.share.<permission> on vault roles and vault globals.
    template<>
    struct PermissionContextPolicyTraits<permission::vault::fs::SharePermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            using Ptr = decltype(&role.fs.files.share);
            if (parts.size() < 5 || parts[0] != "vault" || parts[1] != "fs" || parts[3] != "share")
                return static_cast<Ptr>(nullptr);
            if (parts[2] == "files") return &role.fs.files.share;
            if (parts[2] == "directories") return &role.fs.directories.share;
            return static_cast<Ptr>(nullptr);
        }
    };
}
