#pragma once

#include "rbac/permission/Permission.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/permission/Override.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    enum class PermissionOperation : uint8_t {
        Grant,
        Revoke
    };

    inline permission::OverrideOpt overrideOptFromPermissionOp(const PermissionOperation op) {
        return op == PermissionOperation::Grant ? permission::OverrideOpt::ALLOW : permission::OverrideOpt::DENY;
    }

    struct ResolvedPermissionFlag {
        const permission::Permission* permission{};
        PermissionOperation operation{};
    };

    template<typename Enum, typename SetT>
    bool applyToSet(SetT& set, const permission::Permission& perm, const PermissionOperation op) {
        using RawT = std::underlying_type_t<Enum>;
        const auto value = static_cast<Enum>(static_cast<RawT>(perm.rawValue));

        if (op == PermissionOperation::Grant) set.grant(value);
        else                                  set.revoke(value);

        return true;
    }

    template<typename Enum, typename SetT>
    bool hasInSet(const SetT& set, const permission::Permission& perm) {
        using RawT = std::underlying_type_t<Enum>;
        const auto value = static_cast<Enum>(static_cast<RawT>(perm.rawValue));
        return set.has(value);
    }

    inline std::vector<std::string_view> splitQualifiedName(const std::string_view qn) {
        std::vector<std::string_view> parts;
        std::size_t start = 0;

        while (start < qn.size()) {
            const auto pos = qn.find('.', start);
            if (pos == std::string_view::npos) {
                parts.push_back(qn.substr(start));
                break;
            }

            parts.push_back(qn.substr(start, pos - start));
            start = pos + 1;
        }

        return parts;
    }
}
