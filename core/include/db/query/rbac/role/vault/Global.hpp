#pragma once

#include "db/model/ListQueryParams.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vh::rbac::role::vault { struct Global; }

namespace vh::db::query::rbac::role::vault {

struct Global {
    using GlobalVaultRole = vh::rbac::role::vault::Global;
    using GlobalVaultRolePtr = std::shared_ptr<GlobalVaultRole>;

    static void upsert(const GlobalVaultRolePtr& role);
    static void add(const GlobalVaultRolePtr& role);
    static void update(const GlobalVaultRolePtr& role);

    static void remove(unsigned int userId, const std::string& scope);
    static void removeAllForUser(unsigned int userId);

    static GlobalVaultRolePtr get(unsigned int userId, const std::string& scope);
    static bool exists(unsigned int userId, const std::string& scope);

    static std::vector<GlobalVaultRolePtr> listByUser(unsigned int userId, model::ListQueryParams&& params = {});
    static std::vector<GlobalVaultRolePtr> list(model::ListQueryParams&& params = {});
};

}
