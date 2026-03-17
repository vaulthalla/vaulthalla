#pragma once

#include "db/model/ListQueryParams.hpp"

#include <memory>
#include <vector>
#include <string>

namespace vh::rbac::permission { struct Permission; }

namespace vh::db::query::rbac {

struct Permission {
    static std::shared_ptr<vh::rbac::permission::Permission> getPermission(unsigned int id);
    static std::shared_ptr<vh::rbac::permission::Permission> getPermissionByName(const std::string& name);
    static std::vector<std::shared_ptr<vh::rbac::permission::Permission>> listPermissions();
    static unsigned short countPermissions();
};

}
