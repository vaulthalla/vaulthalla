#pragma once

#include "rbac/permission/admin/identities/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::identities {

struct Users final : IdentitiesBase {
    void operator=(const IdentitiesBase& base) {  IdentitiesBase::operator=(base); }
};

void to_json(nlohmann::json& j, const Users& u);
void from_json(const nlohmann::json& j, Users& u);

}
