#pragma once

#include "rbac/permission/admin/identities/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::identities {

struct Admins final : Base {
    static constexpr const auto* FLAG_CONTEXT = "admins";
    static constexpr const auto* DESCRIPTION_CONTEXT = "admin";

    [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
    [[nodiscard]] std::string_view descriptionObject() const override { return DESCRIPTION_CONTEXT; }
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    static Admins None() {
        Admins a;
        a.clear();
        return a;
    }

    static Admins ViewOnly() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::View);
        return a;
    }

    static Admins Creator() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::View);
        a.grant(IdentityPermissions::Add);
        return a;
    }

    static Admins Editor() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::View);
        a.grant(IdentityPermissions::Edit);
        return a;
    }

    static Admins Deleter() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::View);
        a.grant(IdentityPermissions::Delete);
        return a;
    }

    static Admins Manager() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::View);
        a.grant(IdentityPermissions::Add);
        a.grant(IdentityPermissions::Edit);
        return a;
    }

    static Admins Lifecycle() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::View);
        a.grant(IdentityPermissions::Add);
        a.grant(IdentityPermissions::Delete);
        return a;
    }

    static Admins Full() {
        Admins a;
        a.clear();
        a.grant(IdentityPermissions::All);
        return a;
    }
};

void to_json(nlohmann::json& j, const Admins& admins);
void from_json(const nlohmann::json& j, Admins& admins);

}
