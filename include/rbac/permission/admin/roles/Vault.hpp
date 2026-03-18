#pragma once

#include <rbac/permission/admin/roles/Base.hpp>

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::roles {

    struct Vault : Base {
        static constexpr const auto* FLAG_CONTEXT = "vault";
        static constexpr const auto* DESCRIPTION_CONTEXT = "Vault";

        [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
        [[nodiscard]] std::string toString(uint8_t indent) const override;
        [[nodiscard]] std::string_view descriptionObject() const override { return DESCRIPTION_CONTEXT; }

        static Vault None() {
            Vault v;
            v.clear();
            return v;
        }

        static Vault View() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::View);
            return v;
        }

        static Vault Add() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::View);
            v.grant(RolesPermissions::Add);
            return v;
        }

        static Vault Edit() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::View);
            v.grant(RolesPermissions::Edit);
            return v;
        }

        static Vault Delete() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::View);
            v.grant(RolesPermissions::Delete);
            return v;
        }

        static Vault Manager() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::View);
            v.grant(RolesPermissions::Add);
            v.grant(RolesPermissions::Edit);
            return v;
        }

        static Vault Lifecycle() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::View);
            v.grant(RolesPermissions::Add);
            v.grant(RolesPermissions::Delete);
            return v;
        }

        static Vault Full() {
            Vault v;
            v.clear();
            v.grant(RolesPermissions::All);
            return v;
        }
    };

    void to_json(nlohmann::json& j, const Vault& admin);
    void from_json(const nlohmann::json& j, Vault& admin);
}
