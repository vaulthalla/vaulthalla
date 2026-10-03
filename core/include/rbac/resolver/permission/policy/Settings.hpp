#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

#include <string_view>
#include <vector>

namespace vh::rbac::resolver {
    // admin.settings.<section>.<permission>; every section is a settings::Base.
    template<>
    struct PermissionContextPolicyTraits<permission::admin::settings::SettingsPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT, typename Base = std::conditional_t<std::is_const_v<RoleT>,
            const permission::admin::settings::Base, permission::admin::settings::Base>>
        static Base* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4 || parts[0] != "admin" || parts[1] != "settings") return nullptr;
            auto& s = role.settings;
            if (parts[2] == "websocket") return &s.websocket;
            if (parts[2] == "http") return &s.http;
            if (parts[2] == "database") return &s.database;
            if (parts[2] == "auth") return &s.auth;
            if (parts[2] == "logging") return &s.logging;
            if (parts[2] == "caching") return &s.caching;
            if (parts[2] == "sharing") return &s.sharing;
            if (parts[2] == "services") return &s.services;
            return nullptr;
        }
    };
}
