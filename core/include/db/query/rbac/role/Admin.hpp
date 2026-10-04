#pragma once

#include "db/model/ListQueryParams.hpp"
#include "rbac/Fwd.hpp"

#include <memory>
#include <vector>
#include <string>

namespace vh::db::query::rbac::role {

class Admin {
    using AdminRole = vh::rbac::role::Admin;
    using AdminRolePtr = std::shared_ptr<AdminRole>;

public:
    // Insert-only: never touches an existing row. Throws pqxx::unique_violation when the name is taken.
    static unsigned int insert(const AdminRolePtr& role);

    static unsigned int upsert(const AdminRolePtr& role);

    static void remove(unsigned int id);
    static void remove(const AdminRolePtr& role);

    static AdminRolePtr get(unsigned int id);
    static AdminRolePtr get(const std::string& name);

    static bool exists(unsigned int id);
    static bool exists(const std::string& name);

    static std::vector<AdminRolePtr> list(model::ListQueryParams&& params = {});
};

}
