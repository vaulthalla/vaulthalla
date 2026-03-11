#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>

namespace vh::rbac::permission::admin {

enum class AuditPermissions : uint8_t {
    None = 0,
    View = 1 << 0,
};

struct Audits : Set<AuditPermissions, uint8_t> {
    Audits() = default;
    explicit Audits(const Mask& mask) : Set(mask) {}

    [[nodiscard]] bool canView() const noexcept { return has(AuditPermissions::View); }
};

}
