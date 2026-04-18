#pragma once

#include "db/model/ListQueryParams.hpp"

#include <memory>
#include <vector>
#include <string>

namespace vh::rbac::role { struct Vault; }

namespace vh::db::query::rbac::role {

class Vault {
    using VaultRole = vh::rbac::role::Vault;
    using VaultRolePtr = std::shared_ptr<VaultRole>;

public:
    static unsigned int upsert(const VaultRolePtr& role);

    static void remove(unsigned int id);
    static void remove(const VaultRolePtr& role);

    static VaultRolePtr get(unsigned int id);
    static VaultRolePtr get(const std::string& name);

    static bool exists(unsigned int id);
    static bool exists(const std::string& name);

    static std::vector<VaultRolePtr> list(model::ListQueryParams&& params = {});
};

}
