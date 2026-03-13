#pragma once

#include "rbac/permission/admin/identities/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::identities {

struct Users final : Base {
    [[nodiscard]] std::string toString(uint8_t indent) const override;
};

void to_json(nlohmann::json& j, const Users& u);
void from_json(const nlohmann::json& j, Users& u);

}
