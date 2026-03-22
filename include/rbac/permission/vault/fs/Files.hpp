#pragma once

#include "Share.hpp"
#include "rbac/permission/template/ModuleSet.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <array>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace vault::fs {
        enum class FilePermissions : uint16_t {
            None = 0,
            Preview = 1 << 0,
            Upload = 1 << 1,
            Download = 1 << 2,
            Overwrite = 1 << 3,
            Rename = 1 << 4,
            Delete = 1 << 5,
            Move = 1 << 6,
            Copy = 1 << 7,
            All = Preview | Upload | Download | Overwrite | Rename | Delete | Move | Copy
        };
    }

    template<>
    struct PermissionTraits<vault::fs::FilePermissions> {
        using Entry = PermissionEntry<vault::fs::FilePermissions>;

        static constexpr std::array entries{
            Entry{vault::fs::FilePermissions::Preview, "preview", "Allows previewing files without downloading them."},
            Entry{vault::fs::FilePermissions::Upload, "upload", "Allows uploading new files to the vault."},
            Entry{vault::fs::FilePermissions::Download, "download", "Allows downloading files from the vault."},
            Entry{vault::fs::FilePermissions::Overwrite, "overwrite", "Allows overwriting existing files in the vault."},
            Entry{vault::fs::FilePermissions::Rename, "rename", "Allows renaming files in the vault."},
            Entry{vault::fs::FilePermissions::Delete, "delete", "Allows deleting files from the vault."},
            Entry{vault::fs::FilePermissions::Move, "move", "Allows moving files within the vault."},
            Entry{vault::fs::FilePermissions::Copy, "copy", "Allows copying files within the vault."},
        };
    };

    namespace vault::fs {
        struct Files final : ModuleSet<uint32_t, FilePermissions, uint16_t> {
            static constexpr const auto* ModuleName = "Files";
            static constexpr const auto* FLAG_PREFIX = "files";

            Share share{std::string(FLAG_PREFIX)};

            Files() = default;
            explicit Files(const Mask& mask) { fromMask(mask); }

            [[nodiscard]] const char* name() const override { return ModuleName; }
            [[nodiscard]] std::string toString(uint8_t indent) const override;
            [[nodiscard]] const char* flagPrefix() const override { return FLAG_PREFIX; }

            [[nodiscard]] std::string toFlagsString() const override;
            [[nodiscard]] std::vector<std::string> getFlags() const override { return getFlagsWithOwn(share); }

            [[nodiscard]] uint32_t toMask() const override { return packWithOwn(share); }
            void fromMask(const Mask mask) override { unpackWithOwn(mask, share); }

            [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
                return packAndExportWithOwn(
                    "vault.fs.files",
                    mount("vault.fs.files.share", share)
                );
            }

            [[nodiscard]] bool canPreview() const noexcept { return has(FilePermissions::Preview); }
            [[nodiscard]] bool canUpload() const noexcept { return has(FilePermissions::Upload); }
            [[nodiscard]] bool canDownload() const noexcept { return has(FilePermissions::Download); }
            [[nodiscard]] bool canOverwrite() const noexcept { return has(FilePermissions::Overwrite); }
            [[nodiscard]] bool canRename() const noexcept { return has(FilePermissions::Rename); }
            [[nodiscard]] bool canDelete() const noexcept { return has(FilePermissions::Delete); }
            [[nodiscard]] bool canMove() const noexcept { return has(FilePermissions::Move); }
            [[nodiscard]] bool canCopy() const noexcept { return has(FilePermissions::Copy); }

            [[nodiscard]] bool canShareInternally() const noexcept { return share.canShareInternally(); }
            [[nodiscard]] bool canSharePublicly() const noexcept { return share.canSharePublicly(); }
            [[nodiscard]] bool canSharePubliclyWithVal() const noexcept { return share.canSharePubliclyWithValidation(); }

            static Files None() {
                Files f;
                f.clear();
                f.share = Share::None();
                return f;
            }

            static Files PreviewOnly() {
                Files f;
                f.clear();
                f.grant(FilePermissions::Preview);
                f.share = Share::None();
                return f;
            }

            static Files DownloadOnly() {
                Files f;
                f.clear();
                f.grant(FilePermissions::Preview);
                f.grant(FilePermissions::Download);
                f.share = Share::None();
                return f;
            }

            static Files ReadOnly() {
                Files f;
                f.clear();
                f.grant(FilePermissions::Preview);
                f.grant(FilePermissions::Download);
                f.grant(FilePermissions::Copy);
                f.share = Share::InternalOnly();
                return f;
            }

            static Files Contributor() {
                Files f;
                f.clear();
                f.grant(FilePermissions::Preview);
                f.grant(FilePermissions::Upload);
                f.grant(FilePermissions::Download);
                f.grant(FilePermissions::Overwrite);
                f.grant(FilePermissions::Copy);
                f.share = Share::InternalOnly();
                return f;
            }

            static Files Editor() {
                Files f;
                f.clear();
                f.grant(FilePermissions::Preview);
                f.grant(FilePermissions::Upload);
                f.grant(FilePermissions::Download);
                f.grant(FilePermissions::Overwrite);
                f.grant(FilePermissions::Rename);
                f.grant(FilePermissions::Move);
                f.grant(FilePermissions::Copy);
                f.share = Share::InternalAndValidatedPublic();
                return f;
            }

            static Files Manager() {
                Files f;
                f.clear();
                f.grant(FilePermissions::Preview);
                f.grant(FilePermissions::Upload);
                f.grant(FilePermissions::Download);
                f.grant(FilePermissions::Overwrite);
                f.grant(FilePermissions::Rename);
                f.grant(FilePermissions::Delete);
                f.grant(FilePermissions::Move);
                f.grant(FilePermissions::Copy);
                f.share = Share::InternalAndValidatedPublic();
                return f;
            }

            static Files Full() {
                Files f;
                f.clear();
                f.grant(FilePermissions::All);
                f.share = Share::Full();
                return f;
            }

            static Files Custom(const SetMask fileMask, Share sharePerms) {
                Files f;
                f.setRawFromSetMask(fileMask);
                f.share = std::move(sharePerms);
                return f;
            }
        };

        void to_json(nlohmann::json& j, const Files& f);
        void from_json(const nlohmann::json& j, Files& f);
    }
}
