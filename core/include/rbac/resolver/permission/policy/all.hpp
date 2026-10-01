#pragma once

// Every PermissionContextPolicyTraits specialization. They must be visible wherever PermissionResolver is
// instantiated (EnumPack.hpp includes this), otherwise dispatch silently finds no target and apply() is a no-op.
#include "rbac/resolver/permission/policy/AdminRoles.hpp"
#include "rbac/resolver/permission/policy/AdminVault.hpp"
#include "rbac/resolver/permission/policy/APIKeys.hpp"
#include "rbac/resolver/permission/policy/Identities.hpp"
#include "rbac/resolver/permission/policy/Settings.hpp"
#include "rbac/resolver/permission/policy/Share.hpp"
