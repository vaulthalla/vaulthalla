#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::admin {

enum class AuditPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
};

struct Audits : Set<AuditPermissions, uint8_t> {
    Audits() = default;
    explicit Audits(const Mask& mask) : Set(mask) {}

    [[nodiscard]] std::string toString(uint8_t indent) const override;

    [[nodiscard]] bool canView() const noexcept { return has(AuditPermissions::View); }
};

void to_json(nlohmann::json& j, const Audits& a);
void from_json(const nlohmann::json& j, Audits& a);

}
