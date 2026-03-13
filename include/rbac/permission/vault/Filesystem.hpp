#pragma once

#include "fs/Files.hpp"
#include "fs/Directories.hpp"
#include "rbac/permission/Override.hpp"

#include <vector>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::rbac::permission::vault {

struct Filesystem {
    fs::Files files;
    fs::Directories directories;
    std::vector<std::shared_ptr<Override>> overrides;

    Filesystem() = default;
    explicit Filesystem(const pqxx::row& row);
};

void to_json(nlohmann::json& j, const Filesystem& f);
void from_json(const nlohmann::json& j, Filesystem& f);

}
