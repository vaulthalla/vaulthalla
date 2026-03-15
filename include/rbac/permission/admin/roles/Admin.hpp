#pragma once

#include <rbac/permission/admin/roles/Base.hpp>

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::roles {

    struct Admin : Base {
        static constexpr const auto* FLAG_CONTEXT = "admin";

        [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
        [[nodiscard]] std::string toString(uint8_t indent) const override;
    };

    void to_json(nlohmann::json& j, const Admin& admin);
    void from_json(const nlohmann::json& j, Admin& admin);
}
