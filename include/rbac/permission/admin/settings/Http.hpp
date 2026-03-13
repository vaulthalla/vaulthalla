#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Http final : Base {
    void operator=(const Base& base) { Base::operator=(base); }
};

void to_json(nlohmann::json& j, const Http& s);
void from_json(const nlohmann::json& j, Http& s);

}
