#pragma once

#include "fs/Files.hpp"
#include "fs/Directories.hpp"
#include "rbac/permission/Override.hpp"

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace pqxx { class row; class result; }

namespace vh::rbac::permission::vault {

struct Filesystem {
    fs::Files files{};
    fs::Directories directories{};
    std::vector<std::shared_ptr<Override>> overrides{};

    Filesystem() = default;
    explicit Filesystem(const pqxx::row& row);
    Filesystem(const pqxx::row& row, const pqxx::result& overrideRes);

    [[nodiscard]] std::string toString(uint8_t indent) const;
    [[nodiscard]] std::string toFlagString() const;

    static Filesystem None() {
        Filesystem fs;
        fs.files = fs::Files::None();
        fs.directories = fs::Directories::None();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem BrowseOnly() {
        Filesystem fs;
        fs.files = fs::Files::PreviewOnly();
        fs.directories = fs::Directories::ListOnly();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem ReadOnly() {
        Filesystem fs;
        fs.files = fs::Files::ReadOnly();
        fs.directories = fs::Directories::ReadOnly();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem DownloadOnly() {
        Filesystem fs;
        fs.files = fs::Files::DownloadOnly();
        fs.directories = fs::Directories::DownloadOnly();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem Contributor() {
        Filesystem fs;
        fs.files = fs::Files::Contributor();
        fs.directories = fs::Directories::Contributor();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem Editor() {
        Filesystem fs;
        fs.files = fs::Files::Editor();
        fs.directories = fs::Directories::Editor();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem Manager() {
        Filesystem fs;
        fs.files = fs::Files::Manager();
        fs.directories = fs::Directories::Manager();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem Full() {
        Filesystem fs;
        fs.files = fs::Files::Full();
        fs.directories = fs::Directories::Full();
        fs.overrides.clear();
        return fs;
    }

    static Filesystem NoSharing() {
        Filesystem fs;
        fs.files = fs::Files::Custom(
            static_cast<fs::Files::SetMask>(fs::FilePermissions::All),
            fs::Share::None()
        );
        fs.directories = fs::Directories::Custom(
            static_cast<fs::Directories::SetMask>(fs::DirectoryPermissions::All),
            fs::Share::None()
        );
        fs.overrides.clear();
        return fs;
    }

    static Filesystem Custom(
        fs::Files files,
        fs::Directories directories,
        std::vector<std::shared_ptr<Override>> overrides = {}
    ) {
        Filesystem fs;
        fs.files = std::move(files);
        fs.directories = std::move(directories);
        fs.overrides = std::move(overrides);
        return fs;
    }
};

void to_json(nlohmann::json& j, const Filesystem& f);
void from_json(const nlohmann::json& j, Filesystem& f);

}
