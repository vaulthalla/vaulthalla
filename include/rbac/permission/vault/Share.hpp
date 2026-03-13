#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault {

enum class SharePermissions : uint8_t {
    None = 0,
    Internal = 1 << 0,
    Public = 1 << 1,
    PublicWithValidation = 1 << 2,
    All = Internal | Public | PublicWithValidation
};

struct Share : Set<SharePermissions, uint8_t> {
    Share() = default;
    explicit Share(const Mask& mask) : Set(mask) {}

    [[nodiscard]] bool canShareInternally() const noexcept {
        return has(SharePermissions::Internal);
    }

    [[nodiscard]] bool canSharePublicly() const noexcept {
        return has(SharePermissions::Public);
    }

    [[nodiscard]] bool canSharePubliclyWithValidation() const noexcept {
        return has(SharePermissions::PublicWithValidation);
    }

    [[nodiscard]] bool all() const noexcept {
        return has(SharePermissions::All);
    }

    [[nodiscard]] bool none() const noexcept {
        return has(SharePermissions::None);
    }
};

void to_json(nlohmann::json& j, const Share& s);
void from_json(const nlohmann::json& j, Share& s);

}