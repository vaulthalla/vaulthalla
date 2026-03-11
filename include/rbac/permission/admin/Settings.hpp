#pragma once

#include "rbac/permission/admin/settings/Websocket.hpp"
#include "rbac/permission/admin/settings/Http.hpp"
#include "rbac/permission/admin/settings/Database.hpp"
#include "rbac/permission/admin/settings/Auth.hpp"
#include "rbac/permission/admin/settings/Logging.hpp"
#include "rbac/permission/admin/settings/Caching.hpp"
#include "rbac/permission/admin/settings/Sharing.hpp"
#include "rbac/permission/admin/settings/Services.hpp"
#include "rbac/permission/template/Module.hpp"

#include <cstdint>

namespace vh::rbac::permission::admin {

struct Settings final : Module<uint64_t> {
    static constexpr const auto* ModuleName = "Settings";

    enum class Type : uint8_t {
        Websocket,
        Http,
        Database,
        Auth,
        Logging,
        Caching,
        Sharing,
        Services,
    };

    settings::Websocket websocket;
    settings::Http http;
    settings::Database database;
    settings::Auth auth;
    settings::Logging logging;
    settings::Caching caching;
    settings::Sharing sharing;
    settings::Services services;

    ~Settings() override = default;
    Settings() = default;
    explicit Settings(const Mask& mask) { fromMask(mask); }

    const char* name() const override { return ModuleName; }

    [[nodiscard]] uint64_t toMask() const override {
        return pack(websocket, http, database, auth, logging, caching, sharing, services);
    }

    void fromMask(const uint64_t mask) override {
        unpack(mask, websocket, http, database, auth, logging, caching, sharing, services);
    }

    [[nodiscard]] bool canView(const Type& type) const;
    [[nodiscard]] bool canEdit(const Type& type) const;
};

}
