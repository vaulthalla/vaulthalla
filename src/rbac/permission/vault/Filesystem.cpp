#include "rbac/permission/vault/Filesystem.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>
#include <ostream>

namespace vh::rbac::permission::vault {

Filesystem::Filesystem(const pqxx::row& row)
    : files(static_cast<typename decltype(files)::Mask>(row["files_permissions"].as<uint64_t>())),
      directories(static_cast<typename decltype(directories)::Mask>(row["directories_permissions"].as<uint64_t>())) {}

Filesystem::Filesystem(const pqxx::row& row, const pqxx::result& overrideRes)
    : files(static_cast<typename decltype(files)::Mask>(row["files_permissions"].as<uint64_t>())),
      directories(static_cast<typename decltype(directories)::Mask>(row["directories_permissions"].as<uint64_t>())) {
    if (!overrideRes.empty()) for (const auto& r : overrideRes) overrides.emplace_back(r);
}

std::string Filesystem::toString(const uint8_t indent) const {
    std::ostringstream oss;
    oss << std::string(indent, ' ') << "Filesystem:\n";
    const auto i = indent + 2;
    oss << files.toString(i);
    oss << directories.toString(i);
    return oss.str();
}

std::string Filesystem::toFlagString() const {
    std::ostringstream oss;
    oss << files.toFlagsString() << " " << directories.toFlagsString();
    return oss.str();
}

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
