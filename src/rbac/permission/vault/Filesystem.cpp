#include "rbac/permission/vault/Filesystem.hpp"

#include <pqxx/row>

vh::rbac::permission::vault::Filesystem::Filesystem(const pqxx::row& row)
    : files_(row["files_permissions"].as<typename decltype(files)::Mask>()),
      directories_(row["directories_permissions"].as<typename decltype(directories)::Mask>()) {}
