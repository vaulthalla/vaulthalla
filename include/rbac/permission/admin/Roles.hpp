#pragma once

#include "rbac/permission/template/Module.hpp"
#include "rbac/permission/admin/roles/Admin.hpp"
#include "rbac/permission/admin/roles/Vault.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin {

    struct Roles : Module<uint16_t> {
        static constexpr const auto* ModuleName = "roles";

        roles::Admin admin;
        roles::Vault vault;

        Roles() = default;
        explicit Roles(const Mask mask) { fromMask(mask); }

        [[nodiscard]] const char *name() const override { return ModuleName; }

        [[nodiscard]] Mask toMask() const override { return pack(admin, vault); }
        void fromMask(const Mask mask) override { unpack(mask, admin, vault); }

        [[nodiscard]] std::string toFlagsString() const override;
        [[nodiscard]] std::string toString(uint8_t indent) const override;
    };

    void to_json(nlohmann::json& j, const Roles& r);
    void from_json(const nlohmann::json& j, Roles& r);

}
