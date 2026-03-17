#pragma once

#include "Share.hpp"
#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/ModuleSet.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <array>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace vault::fs {
        enum class DirectoryPermissions : uint16_t {
            None = 0,
            List = 1 << 0,
            Upload = 1 << 1,
            Download = 1 << 2,
            Touch = 1 << 3,
            Delete = 1 << 4,
            Rename = 1 << 5,
            Move = 1 << 6,
            Copy = 1 << 7,
            All = List | Upload | Download | Touch | Delete | Rename | Move | Copy
        };
    }

    template<>
    struct PermissionTraits<vault::fs::DirectoryPermissions> {
        using Entry = PermissionEntry<vault::fs::DirectoryPermissions>;

        static constexpr std::array entries{
            Entry{vault::fs::DirectoryPermissions::List, "list", "Allow viewing or listing the contents of a directory."},
            Entry{vault::fs::DirectoryPermissions::Upload, "upload", "Allow uploading directories."},
            Entry{vault::fs::DirectoryPermissions::Download, "download", "Allow downloading directories."},
            Entry{vault::fs::DirectoryPermissions::Touch, "touch", "Allow creating empty directories."},
            Entry{vault::fs::DirectoryPermissions::Delete, "delete", "Allow deleting directories."},
            Entry{vault::fs::DirectoryPermissions::Rename, "rename", "Allow renaming directories."},
            Entry{vault::fs::DirectoryPermissions::Move, "move", "Allow moving directories."},
            Entry{vault::fs::DirectoryPermissions::Copy, "copy", "Allow copying directories."}
        };
    };

    namespace vault::fs {
        struct Directories final : ModuleSet<uint32_t, DirectoryPermissions, uint16_t> {
            static constexpr const auto* ModuleName = "Directories";
            static constexpr const auto* FLAG_PREFIX = "dirs";

            Share share;

            Directories() = default;
            explicit Directories(const Mask& mask) { fromMask(mask); }

            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] const char* name() const override { return ModuleName; }
            [[nodiscard]] const char* flagPrefix() const override { return FLAG_PREFIX; }

            [[nodiscard]] std::string toFlagsString() const override;

            [[nodiscard]] uint32_t toMask() const override { return packWithOwn(share); }
            void fromMask(const Mask mask) override { unpackWithOwn(mask, share); }

            [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
                return packAndExportWithOwn(
                    "vault.fs.directories",
                    mount("vault.fs.directories.share", share)
                );
            }

            [[nodiscard]] bool canList() const noexcept { return has(DirectoryPermissions::List); }
            [[nodiscard]] bool canUpload() const noexcept { return has(DirectoryPermissions::Upload); }
            [[nodiscard]] bool canDownload() const noexcept { return has(DirectoryPermissions::Download); }
            [[nodiscard]] bool canTouch() const noexcept { return has(DirectoryPermissions::Touch); }
            [[nodiscard]] bool canDelete() const noexcept { return has(DirectoryPermissions::Delete); }
            [[nodiscard]] bool canRename() const noexcept { return has(DirectoryPermissions::Rename); }
            [[nodiscard]] bool canMove() const noexcept { return has(DirectoryPermissions::Move); }
            [[nodiscard]] bool canCopy() const noexcept { return has(DirectoryPermissions::Copy); }

            [[nodiscard]] bool canShareInternally() const noexcept { return share.canShareInternally(); }
            [[nodiscard]] bool canSharePublicly() const noexcept { return share.canSharePublicly(); }
            [[nodiscard]] bool canSharePubliclyWithVal() const noexcept { return share.canSharePubliclyWithValidation(); }

            [[nodiscard]] bool canMutate() const noexcept {
                return canUpload() || canTouch() || canDelete() || canRename() || canMove();
            }

            static Directories None() {
                Directories d;
                d.clear();
                d.share = Share::None();
                return d;
            }

            static Directories ListOnly() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::List);
                d.share = Share::None();
                return d;
            }

            static Directories DownloadOnly() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::List);
                d.grant(DirectoryPermissions::Download);
                d.share = Share::None();
                return d;
            }

            static Directories ReadOnly() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::List);
                d.grant(DirectoryPermissions::Download);
                d.grant(DirectoryPermissions::Copy);
                d.share = Share::InternalOnly();
                return d;
            }

            static Directories Contributor() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::List);
                d.grant(DirectoryPermissions::Upload);
                d.grant(DirectoryPermissions::Download);
                d.grant(DirectoryPermissions::Touch);
                d.grant(DirectoryPermissions::Copy);
                d.share = Share::InternalOnly();
                return d;
            }

            static Directories Editor() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::List);
                d.grant(DirectoryPermissions::Upload);
                d.grant(DirectoryPermissions::Download);
                d.grant(DirectoryPermissions::Touch);
                d.grant(DirectoryPermissions::Rename);
                d.grant(DirectoryPermissions::Move);
                d.grant(DirectoryPermissions::Copy);
                d.share = Share::InternalAndValidatedPublic();
                return d;
            }

            static Directories Manager() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::List);
                d.grant(DirectoryPermissions::Upload);
                d.grant(DirectoryPermissions::Download);
                d.grant(DirectoryPermissions::Touch);
                d.grant(DirectoryPermissions::Delete);
                d.grant(DirectoryPermissions::Rename);
                d.grant(DirectoryPermissions::Move);
                d.grant(DirectoryPermissions::Copy);
                d.share = Share::InternalAndValidatedPublic();
                return d;
            }

            static Directories Full() {
                Directories d;
                d.clear();
                d.grant(DirectoryPermissions::All);
                d.share = Share::Full();
                return d;
            }

            static Directories Custom(const SetMask dirMask, Share sharePerms) {
                Directories d;
                d.setRawFromSetMask(dirMask);
                d.share = std::move(sharePerms);
                return d;
            }
        };

        void to_json(nlohmann::json& j, const Directories& v);
        void from_json(const nlohmann::json& j, Directories& v);
    }
}
