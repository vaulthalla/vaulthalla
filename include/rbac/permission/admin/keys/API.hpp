#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Module.hpp"

#include <nlohmann/json_fwd.hpp>
#include <utility>

namespace vh::rbac::permission {
    namespace admin::keys {
        enum class APIPermissions : uint8_t {
            None = 0,
            View = 1 << 0,
            Create = 1 << 1,
            Edit = 1 << 2,
            Remove = 1 << 3,
            Export = 1 << 4,
            Rotate = 1 << 5,
            Consume = 1 << 6,
            All = View | Create | Edit | Remove | Export | Rotate | Consume
        };
    }

    template<>
    struct PermissionTraits<admin::keys::APIPermissions> {
        using Entry = PermissionEntry<admin::keys::APIPermissions>;

        static constexpr std::array entries = {
            Entry{admin::keys::APIPermissions::View, "view", "Allows the user to view api-keys scoped to '{}'"},
            Entry{admin::keys::APIPermissions::Create, "create", "Allows the user to create a new API key for '{}'"},
            Entry{admin::keys::APIPermissions::Edit, "edit", "Allows the user to edit a new API key for '{}'"},
            Entry{admin::keys::APIPermissions::Remove, "remove", "Allows the user to remove a new API key for '{}'"},
            Entry{admin::keys::APIPermissions::Export, "export", "Allows the user to export an API key for '{}'"},
            Entry{admin::keys::APIPermissions::Rotate, "rotate", "Allows the user to rotate a new API key for '{}'"},
            Entry{admin::keys::APIPermissions::Consume, "consume", "Allows the user to consume an API key for '{}'"},
        };
    };

    namespace admin::keys {
        struct APIKeyBase final : Set<APIPermissions, uint8_t> {
            std::string flag_prefix;
            std::string description_context;

            APIKeyBase(std::string moduleName, std::string descriptionCtx)
                : flag_prefix(std::move(moduleName) + "-" + descriptionCtx),
                  description_context(std::move(descriptionCtx)) {
            }

            [[nodiscard]] const char *flagPrefix() const override { return flag_prefix.c_str(); }
            [[nodiscard]] std::string_view descriptionObject() const { return description_context; }

            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(APIPermissions::View); }
            [[nodiscard]] bool canCreate() const noexcept { return has(APIPermissions::Create); }
            [[nodiscard]] bool canEdit() const noexcept { return has(APIPermissions::Edit); }
            [[nodiscard]] bool canRemove() const noexcept { return has(APIPermissions::Remove); }
            [[nodiscard]] bool canExport() const noexcept { return has(APIPermissions::Export); }
            [[nodiscard]] bool canRotate() const noexcept { return has(APIPermissions::Rotate); }
            [[nodiscard]] bool canConsume() const noexcept { return has(APIPermissions::Consume); }

            static APIKeyBase None(std::string moduleName, std::string descriptionCtx) {
                APIKeyBase a{std::move(moduleName), std::move(descriptionCtx)};
                a.clear();
                return a;
            }

            static APIKeyBase View(std::string moduleName, std::string descriptionCtx) {
                APIKeyBase a{std::move(moduleName), std::move(descriptionCtx)};
                a.clear();
                a.grant(APIPermissions::View);
                return a;
            }

            static APIKeyBase ViewAndConsume(std::string moduleName, std::string descriptionCtx) {
                APIKeyBase a{std::move(moduleName), std::move(descriptionCtx)};
                a.clear();
                a.grant(APIPermissions::View);
                a.grant(APIPermissions::Consume);
                return a;
            }

            static APIKeyBase Create(std::string moduleName, std::string descriptionCtx) {
                APIKeyBase a{std::move(moduleName), std::move(descriptionCtx)};
                a.clear();
                a.grant(APIPermissions::View);
                a.grant(APIPermissions::Create);
                a.grant(APIPermissions::Edit);
                a.grant(APIPermissions::Remove);
                a.grant(APIPermissions::Consume);
                return a;
            }

            static APIKeyBase Full(std::string moduleName, std::string descriptionCtx) {
                APIKeyBase a{std::move(moduleName), std::move(descriptionCtx)};
                a.clear();
                a.grant(APIPermissions::All);
                return a;
            }
        };

        struct APIKeys {
            static constexpr const auto *MODULE_NAME = "api";

            APIKeyBase self{std::string(MODULE_NAME), "self"};
            APIKeyBase admin{std::string(MODULE_NAME), "admin"};
            APIKeyBase user{std::string(MODULE_NAME), "user"};

            [[nodiscard]] static const char *name() { return MODULE_NAME; }

            [[nodiscard]] std::string toString(uint8_t indent) const;

            [[nodiscard]] std::string toFlagsString() const;

            static APIKeys None() {
                return APIKeys{
                    .self = APIKeyBase::None(MODULE_NAME, "self"),
                    .admin = APIKeyBase::None(MODULE_NAME, "admin"),
                    .user = APIKeyBase::None(MODULE_NAME, "user")
                };
            }

            static APIKeys View() {
                return APIKeys{
                    .self = APIKeyBase::View(MODULE_NAME, "self"),
                    .admin = APIKeyBase::View(MODULE_NAME, "admin"),
                    .user = APIKeyBase::View(MODULE_NAME, "user")
                };
            }

            static APIKeys ViewAndConsume() {
                return APIKeys{
                    .self = APIKeyBase::ViewAndConsume(MODULE_NAME, "self"),
                    .admin = APIKeyBase::ViewAndConsume(MODULE_NAME, "admin"),
                    .user = APIKeyBase::ViewAndConsume(MODULE_NAME, "user")
                };
            }

            static APIKeys Create() {
                return APIKeys{
                    .self = APIKeyBase::Create(MODULE_NAME, "self"),
                    .admin = APIKeyBase::Create(MODULE_NAME, "admin"),
                    .user = APIKeyBase::Create(MODULE_NAME, "user")
                };
            }

            static APIKeys Full() {
                return APIKeys{
                    .self = APIKeyBase::Full(MODULE_NAME, "self"),
                    .admin = APIKeyBase::Full(MODULE_NAME, "admin"),
                    .user = APIKeyBase::Full(MODULE_NAME, "user")
                };
            }

            static APIKeys Custom(APIKeyBase self, APIKeyBase admin, APIKeyBase user) {
                return APIKeys{
                    .self = std::move(self),
                    .admin = std::move(admin),
                    .user = std::move(user)
                };
            }
        };

        void to_json(nlohmann::json &j, const APIKeyBase &p);

        void from_json(const nlohmann::json &j, APIKeyBase &p);

        void to_json(nlohmann::json &j, const APIKeys &p);

        void from_json(const nlohmann::json &j, APIKeys &p);
    }
}
