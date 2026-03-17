#pragma once

#include <rbac/permission/admin/roles/Base.hpp>

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::roles {

    struct Admin final : Base {
        static constexpr const auto* FLAG_CONTEXT = "admin";
        static constexpr const auto* DESCRIPTION_CONTEXT = "Admin";

        [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
        [[nodiscard]] std::string toString(uint8_t indent) const override;
        [[nodiscard]] std::string_view descriptionObject() const override { return DESCRIPTION_CONTEXT; }

        static Admin None() {
            Admin a;
            a.clear();
            return a;
        }

        static Admin View() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::View);
            return a;
        }

        static Admin Add() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::View);
            a.grant(RolesPermissions::Add);
            return a;
        }

        static Admin Edit() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::View);
            a.grant(RolesPermissions::Edit);
            return a;
        }

        static Admin Delete() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::View);
            a.grant(RolesPermissions::Delete);
            return a;
        }

        static Admin Manager() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::View);
            a.grant(RolesPermissions::Add);
            a.grant(RolesPermissions::Edit);
            return a;
        }

        static Admin Lifecycle() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::View);
            a.grant(RolesPermissions::Add);
            a.grant(RolesPermissions::Delete);
            return a;
        }

        static Admin Full() {
            Admin a;
            a.clear();
            a.grant(RolesPermissions::All);
            return a;
        }
    };

    void to_json(nlohmann::json& j, const Admin& admin);
    void from_json(const nlohmann::json& j, Admin& admin);
}
