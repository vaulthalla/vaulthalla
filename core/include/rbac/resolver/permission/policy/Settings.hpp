#pragma once

#include "rbac/resolver/permission/Traits.hpp"
#include "rbac/role/Admin.hpp"

#include <vector>
#include <string_view>

namespace vh::rbac::resolver {
    template<>
    struct PermissionContextPolicyTraits<permission::admin::settings::SettingsPermissions> {
        static constexpr bool enabled = true;

        template<typename RoleT>
        static auto* resolve(RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.settings.websocket)>(nullptr);

            if (parts[0] != "admin")    return static_cast<decltype(&role.settings.websocket)>(nullptr);
            if (parts[1] != "settings") return static_cast<decltype(&role.settings.websocket)>(nullptr);

            if (parts[2] == "websocket") return &role.settings.websocket;
            if (parts[2] == "http")      return &role.settings.http;
            if (parts[2] == "database")  return &role.settings.database;
            if (parts[2] == "auth")      return &role.settings.auth;
            if (parts[2] == "logging")   return &role.settings.logging;
            if (parts[2] == "caching")   return &role.settings.caching;
            if (parts[2] == "sharing")   return &role.settings.sharing;
            if (parts[2] == "services")  return &role.settings.services;

            return static_cast<decltype(&role.settings.websocket)>(nullptr);
        }

        template<typename RoleT>
        static const auto* resolve(const RoleT& role, const std::vector<std::string_view>& parts) {
            if (parts.size() < 4) return static_cast<decltype(&role.settings.websocket)>(nullptr);

            if (parts[0] != "admin")    return static_cast<decltype(&role.settings.websocket)>(nullptr);
            if (parts[1] != "settings") return static_cast<decltype(&role.settings.websocket)>(nullptr);

            if (parts[2] == "websocket") return &role.settings.websocket;
            if (parts[2] == "http")      return &role.settings.http;
            if (parts[2] == "database")  return &role.settings.database;
            if (parts[2] == "auth")      return &role.settings.auth;
            if (parts[2] == "logging")   return &role.settings.logging;
            if (parts[2] == "caching")   return &role.settings.caching;
            if (parts[2] == "sharing")   return &role.settings.sharing;
            if (parts[2] == "services")  return &role.settings.services;

            return static_cast<decltype(&role.settings.websocket)>(nullptr);
        }
    };
}
