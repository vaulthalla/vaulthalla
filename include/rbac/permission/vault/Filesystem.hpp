#pragma once

#include "fs/Directories.hpp"
#include "fs/Files.hpp"
#include "rbac/permission/Override.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace pqxx {
    class row;
    class result;
}

namespace vh::rbac::permission {
    namespace vault {
        enum class FilesystemAction : uint16_t {
            Preview,
            List,
            Upload,
            Download,
            Touch,
            Overwrite,
            Rename,
            Delete,
            Move,
            Copy,
            ShareInternal,
            SharePublic,
            SharePublicValidated,
            Lookup
        };
    }

    template<>
    struct PermissionTraits<vault::FilesystemAction> {
        using Entry = PermissionEntry<vault::FilesystemAction>;

        static constexpr std::array entries{
            Entry{
                vault::FilesystemAction::Preview,
                "preview",
                "Allows previewing files without downloading them."
            },
            Entry{
                vault::FilesystemAction::List,
                "list",
                "Allows listing directory contents."
            },
            Entry{
                vault::FilesystemAction::Upload,
                "upload",
                "Allows uploading files or directories."
            },
            Entry{
                vault::FilesystemAction::Download,
                "download",
                "Allows downloading files or directories."
            },
            Entry{
                vault::FilesystemAction::Touch,
                "touch",
                "Allows creating empty directories."
            },
            Entry{
                vault::FilesystemAction::Overwrite,
                "overwrite",
                "Allows overwriting existing files."
            },
            Entry{
                vault::FilesystemAction::Rename,
                "rename",
                "Allows renaming files or directories."
            },
            Entry{
                vault::FilesystemAction::Delete,
                "delete",
                "Allows deleting files or directories."
            },
            Entry{
                vault::FilesystemAction::Move,
                "move",
                "Allows moving files or directories."
            },
            Entry{
                vault::FilesystemAction::Copy,
                "copy",
                "Allows copying files or directories."
            },
            Entry{
                vault::FilesystemAction::ShareInternal,
                "share_internal",
                "Allows sharing files or directories internally within the vault."
            },
            Entry{
                vault::FilesystemAction::SharePublic,
                "share_public",
                "Allows sharing files or directories publicly without validation."
            },
            Entry{
                vault::FilesystemAction::SharePublicValidated,
                "share_public_validated",
                "Allows sharing files or directories publicly with validation."
            },
            Entry{
                vault::FilesystemAction::Lookup,
                "lookup",
                "Allows looking up files or directories by name or metadata."
            }
        };
    };

    namespace vault {
        struct Filesystem {
            fs::Files files{};
            fs::Directories directories{};
            std::vector<Override> overrides{};

            Filesystem() = default;

            explicit Filesystem(const pqxx::row &row);

            Filesystem(const pqxx::row &row, const pqxx::result &overrideRes);

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
                    fs::Share::None(fs::Files::FLAG_PREFIX)
                );
                fs.directories = fs::Directories::Custom(
                    static_cast<fs::Directories::SetMask>(fs::DirectoryPermissions::All),
                    fs::Share::None(fs::Directories::FLAG_PREFIX)
                );
                fs.overrides.clear();
                return fs;
            }

            static Filesystem Custom(
                fs::Files files,
                fs::Directories directories,
                std::vector<Override> overrides = {}
            ) {
                Filesystem fs;
                fs.files = std::move(files);
                fs.directories = std::move(directories);
                fs.overrides = std::move(overrides);
                return fs;
            }
        };

        void to_json(nlohmann::json &j, const Filesystem &f);

        void from_json(const nlohmann::json &j, Filesystem &f);
    }
}
