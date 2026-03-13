#pragma once

#include "rbac/permission/admin/settings/Base.hpp"

#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin::settings {

struct Websocket final : Base {
    void operator=(const Base& base) { Base::operator=(base); }
};

void to_json(nlohmann::json& j, const Websocket& s);
void from_json(const nlohmann::json& j, Websocket& s);

}
