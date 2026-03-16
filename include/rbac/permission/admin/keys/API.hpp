#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Module.hpp"

#include <nlohmann/json_fwd.hpp>
#include <utility>

namespace vh::rbac::permission {

    namespace admin::keys {
        enum class APIPermissions : uint8_t {
            None   = 0,
            View   = 1 << 0,
            Create = 1 << 1,
            Edit   = 1 << 2,
            Remove = 1 << 3,
            Export = 1 << 4,
            Rotate = 1 << 5,
            Consume = 1 << 6,
        };
    }

    template <>
    struct PermissionTraits<admin::keys::APIPermissions> {
        using Entry = PermissionEntry<admin::keys::APIPermissions>;

        static constexpr std::array entries = {
            Entry{admin::keys::APIPermissions::View, "view"},
            Entry{admin::keys::APIPermissions::Create, "create"},
            Entry{admin::keys::APIPermissions::Edit, "edit"},
            Entry{admin::keys::APIPermissions::Remove, "remove"},
            Entry{admin::keys::APIPermissions::Export, "export"},
            Entry{admin::keys::APIPermissions::Rotate, "rotate"},
            Entry{admin::keys::APIPermissions::Consume, "consume"}
        };
    };

    namespace admin::keys {
        struct APIKeyBase final : Set<APIPermissions, uint8_t> {
            std::string flag_prefix;

            explicit APIKeyBase(std::string  extension) : flag_prefix(std::move(extension)) {}

            [[nodiscard]] const char *flagPrefix() const override { return flag_prefix.c_str(); }
            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(APIPermissions::View); }
            [[nodiscard]] bool canCreate() const noexcept { return has(APIPermissions::Create); }
            [[nodiscard]] bool canEdit() const noexcept { return has(APIPermissions::Edit); }
            [[nodiscard]] bool canRemove() const noexcept { return has(APIPermissions::Remove); }
            [[nodiscard]] bool canExport() const noexcept { return has(APIPermissions::Export); }
            [[nodiscard]] bool canRotate() const noexcept { return has(APIPermissions::Rotate); }
            [[nodiscard]] bool canConsume() const noexcept { return has(APIPermissions::Consume); }
        };

        struct APIKeys {
            static constexpr const auto* MODULE_NAME = "api";

            APIKeyBase self{std::string(MODULE_NAME) + "-self"};
            APIKeyBase admin{std::string(MODULE_NAME) + "-admin"};
            APIKeyBase user{std::string(MODULE_NAME) + "-user"};

            [[nodiscard]] static const char *name() { return MODULE_NAME; }
            [[nodiscard]] std::string toString(uint8_t indent) const;
            [[nodiscard]] std::string toFlagsString() const;
        };

        void to_json(nlohmann::json& j, const APIKeyBase& p);
        void from_json(const nlohmann::json& j, APIKeyBase& p);

        void to_json(nlohmann::json& j, const APIKeys& p);
        void from_json(const nlohmann::json& j, APIKeys& p);
    }

}
