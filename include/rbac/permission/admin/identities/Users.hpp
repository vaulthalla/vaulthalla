#pragma once

#include "rbac/permission/admin/identities/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::identities {

    struct Users final : Base {
        static constexpr const auto* FLAG_CONTEXT = "users";
        static constexpr const auto* DESCRIPTION_CONTEXT = "user";

        [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
        [[nodiscard]] std::string_view descriptionObject() const override { return DESCRIPTION_CONTEXT; }
        [[nodiscard]] std::string toString(uint8_t indent) const override;

        static Users None() {
            Users u;
            u.clear();
            return u;
        }

        static Users ViewOnly() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::View);
            return u;
        }

        static Users Creator() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::View);
            u.grant(IdentityPermissions::Add);
            return u;
        }

        static Users Editor() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::View);
            u.grant(IdentityPermissions::Edit);
            return u;
        }

        static Users Deleter() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::View);
            u.grant(IdentityPermissions::Delete);
            return u;
        }

        static Users Manager() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::View);
            u.grant(IdentityPermissions::Add);
            u.grant(IdentityPermissions::Edit);
            return u;
        }

        static Users Lifecycle() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::View);
            u.grant(IdentityPermissions::Add);
            u.grant(IdentityPermissions::Delete);
            return u;
        }

        static Users Full() {
            Users u;
            u.clear();
            u.grant(IdentityPermissions::All);
            return u;
        }
    };

    void to_json(nlohmann::json& j, const Users& u);
    void from_json(const nlohmann::json& j, Users& u);

}
