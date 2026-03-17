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
#include <nlohmann/json_fwd.hpp>

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

    settings::Websocket websocket{};
    settings::Http http{};
    settings::Database database{};
    settings::Auth auth{};
    settings::Logging logging{};
    settings::Caching caching{};
    settings::Sharing sharing{};
    settings::Services services{};

    ~Settings() override = default;
    Settings() = default;
    explicit Settings(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toFlagsString() const override;
    [[nodiscard]] std::string toString(uint8_t indent) const override;
    [[nodiscard]] const char* name() const override { return ModuleName; }

    [[nodiscard]] uint64_t toMask() const override {
        return pack(websocket, http, database, auth, logging, caching, sharing, services);
    }

    void fromMask(const uint64_t mask) override {
        unpack(mask, websocket, http, database, auth, logging, caching, sharing, services);
    }

    [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
        return packAndExportPerms(
            mount("admin.settings.websocket", websocket),
            mount("admin.settings.http", http),
            mount("admin.settings.database", database),
            mount("admin.settings.auth", auth),
            mount("admin.settings.logging", logging),
            mount("admin.settings.caching", caching),
            mount("admin.settings.sharing", sharing),
            mount("admin.settings.services", services)
        );
    }

    [[nodiscard]] bool canView(const Type& type) const;
    [[nodiscard]] bool canEdit(const Type& type) const;

    static Settings None() {
        Settings s;
        s.websocket = settings::Websocket::None();
        s.http = settings::Http::None();
        s.database = settings::Database::None();
        s.auth = settings::Auth::None();
        s.logging = settings::Logging::None();
        s.caching = settings::Caching::None();
        s.sharing = settings::Sharing::None();
        s.services = settings::Services::None();
        return s;
    }

    static Settings ViewOnly() {
        Settings s;
        s.websocket = settings::Websocket::View();
        s.http = settings::Http::View();
        s.database = settings::Database::View();
        s.auth = settings::Auth::View();
        s.logging = settings::Logging::View();
        s.caching = settings::Caching::View();
        s.sharing = settings::Sharing::View();
        s.services = settings::Services::View();
        return s;
    }

    static Settings Support() {
        auto s = None();
        s.websocket = settings::Websocket::View();
        s.http = settings::Http::View();
        s.logging = settings::Logging::View();
        s.caching = settings::Caching::View();
        s.services = settings::Services::View();
        return s;
    }

    static Settings OperationsAdmin() {
        auto s = None();
        s.websocket = settings::Websocket::Edit();
        s.http = settings::Http::Edit();
        s.services = settings::Services::Edit();
        s.caching = settings::Caching::Edit();
        s.logging = settings::Logging::View();
        return s;
    }

    static Settings SecurityAuditor() {
        auto s = None();
        s.auth = settings::Auth::View();
        s.logging = settings::Logging::View();
        s.sharing = settings::Sharing::View();
        return s;
    }

    static Settings SecurityAdmin() {
        auto s = None();
        s.auth = settings::Auth::Edit();
        s.sharing = settings::Sharing::Edit();
        s.logging = settings::Logging::View();
        return s;
    }

    static Settings DatabaseAdmin() {
        auto s = None();
        s.database = settings::Database::Edit();
        s.caching = settings::Caching::Edit();
        s.logging = settings::Logging::View();
        return s;
    }

    static Settings SharingAdmin() {
        auto s = None();
        s.sharing = settings::Sharing::Edit();
        s.auth = settings::Auth::View();
        s.logging = settings::Logging::View();
        return s;
    }

    static Settings Full() {
        Settings s;
        s.websocket = settings::Websocket::Edit();
        s.http = settings::Http::Edit();
        s.database = settings::Database::Edit();
        s.auth = settings::Auth::Edit();
        s.logging = settings::Logging::Edit();
        s.caching = settings::Caching::Edit();
        s.sharing = settings::Sharing::Edit();
        s.services = settings::Services::Edit();
        return s;
    }

    static Settings Custom(
        settings::Websocket websocket,
        settings::Http http,
        settings::Database database,
        settings::Auth auth,
        settings::Logging logging,
        settings::Caching caching,
        settings::Sharing sharing,
        settings::Services services
    ) {
        Settings s;
        s.websocket = std::move(websocket);
        s.http = std::move(http);
        s.database = std::move(database);
        s.auth = std::move(auth);
        s.logging = std::move(logging);
        s.caching = std::move(caching);
        s.sharing = std::move(sharing);
        s.services = std::move(services);
        return s;
    }
};

void to_json(nlohmann::json& j, const Settings& o);
void from_json(const nlohmann::json& j, Settings& o);

}
