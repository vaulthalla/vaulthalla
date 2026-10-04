#pragma once

#include "db/model/ListQueryParams.hpp"
#include "rbac/Fwd.hpp"

#include <memory>
#include <vector>
#include <string>
#include <pqxx/pqxx>

namespace vh::db::query::rbac::role {

class Vault {
    using VaultRole = vh::rbac::role::Vault;
    using VaultRolePtr = std::shared_ptr<VaultRole>;

public:
    // Insert-only: never touches an existing row. Throws pqxx::unique_violation when the name is taken.
    static unsigned int insert(const VaultRolePtr& role);

    static unsigned int upsert(const VaultRolePtr& role);
    static unsigned int upsert(pqxx::work& txn, const VaultRolePtr& role);

    static void remove(unsigned int id);
    static void remove(const VaultRolePtr& role);

    static VaultRolePtr get(unsigned int id);
    static VaultRolePtr get(const std::string& name);

    static bool exists(unsigned int id);
    static bool exists(const std::string& name);

    static std::vector<VaultRolePtr> list(model::ListQueryParams&& params = {});
};

}
