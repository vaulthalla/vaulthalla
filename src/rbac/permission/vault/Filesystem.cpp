#include "rbac/permission/vault/Filesystem.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>

namespace vh::rbac::permission::vault {

Filesystem::Filesystem(const pqxx::row& row)
    : files(row["files_permissions"].as<typename decltype(files)::Mask>()),
      directories(row["directories_permissions"].as<typename decltype(directories)::Mask>()) {}

void to_json(nlohmann::json& j, const Filesystem& f) {
    j = {
        {"files", f.files},
        {"directories", f.directories},
    };
}

void from_json(const nlohmann::json& j, Filesystem& f) {
    j.at("files").get_to(f.files);
    j.at("directories").get_to(f.directories);
}

}
