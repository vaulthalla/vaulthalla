#pragma once

// Forward declarations for RBAC roles and permissions. Declarations only: include the defining header to use a type.

namespace vh::rbac::role {
    struct Admin;
    struct Vault;
}

namespace vh::rbac::permission {
    struct Override;
}
