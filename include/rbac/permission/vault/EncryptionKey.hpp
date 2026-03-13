#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault {

enum class EncryptionKeyPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
    Export = 1 << 1,
    Rotate = 1 << 2,
    All = View | Export | Rotate
};

struct EncryptionKey final : Set<EncryptionKeyPermissions, uint8_t> {
    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] bool canView() const noexcept { return has(EncryptionKeyPermissions::View); }
    [[nodiscard]] bool canExport() const noexcept { return has(EncryptionKeyPermissions::Export); }
    [[nodiscard]] bool canRotate() const noexcept { return has(EncryptionKeyPermissions::Rotate); }
    [[nodiscard]] bool any() const noexcept { return has(EncryptionKeyPermissions::All); }
    [[nodiscard]] bool none() const noexcept { return has(EncryptionKeyPermissions::None); }
};

void to_json(nlohmann::json& j, const EncryptionKey& k);
void from_json(const nlohmann::json& j, EncryptionKey& k);

}
