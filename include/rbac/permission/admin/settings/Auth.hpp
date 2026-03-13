#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Auth final : Base {
    [[nodiscard]] std::string toString(uint8_t indent) const override;
};

void to_json(nlohmann::json& j, const Auth& s);
void from_json(const nlohmann::json& j, Auth& s);

}
