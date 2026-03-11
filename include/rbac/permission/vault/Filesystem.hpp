#pragma once

#include "rbac/permission/vault/Files.hpp"
#include "rbac/permission/vault/Directories.hpp"
#include "rbac/permission/Override.hpp"

#include <vector>
#include <memory>

namespace pqxx { class row; }

namespace vh::rbac::permission::vault {

class Filesystem {
    Files files_;
    Directories directories_;
    std::vector<std::shared_ptr<Override>> overrides;

public:
    Filesystem() = default;
    explicit Filesystem(const pqxx::row& row);
};

}
