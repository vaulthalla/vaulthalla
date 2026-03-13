#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/Override.hpp"

#include <cstdint>
#include <memory>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault {

enum class APIKeyPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    ViewSecret = 1 << 1,
    Modify = 1 << 2,
    All = View | ViewSecret | Modify
};

struct APIKey : Set<APIKeyPermissions, uint8_t> {
    std::vector<std::shared_ptr<Override>> overrides;

    [[nodiscard]] bool canView() const noexcept { return has(APIKeyPermissions::View); }
    [[nodiscard]] bool canViewSecret() const noexcept { return has(APIKeyPermissions::ViewSecret); }
    [[nodiscard]] bool canModify() const noexcept { return has(APIKeyPermissions::Modify); }
    [[nodiscard]] bool any() const noexcept { return has(APIKeyPermissions::All); }
    [[nodiscard]] bool none() const noexcept { return has(APIKeyPermissions::None); }
};

void to_json(nlohmann::json& j, const APIKey& k);
void from_json(const nlohmann::json& j, APIKey& k);

}
