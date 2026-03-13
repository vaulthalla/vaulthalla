#pragma once

#include "rbac/permission/vault/Share.hpp"
#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/ModuleSet.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission::vault::fs {

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

struct Directories final : ModuleSet<uint32_t, DirectoryPermissions, uint16_t> {
    static constexpr const auto* ModuleName = "Directories";

    Share share;

    Directories() = default;
    explicit Directories(const Mask& mask) { fromMask(mask); }

    [[nodiscard]] std::string toString(uint8_t indent) const override;

    const char* name() const override { return ModuleName; }

    [[nodiscard]] uint32_t toMask() const override { return pack(permissions, share); }
    void fromMask(const Mask mask) override { unpack(mask, permissions, share); }

    [[nodiscard]] bool canList() const noexcept { return has(DirectoryPermissions::List); }
    [[nodiscard]] bool canUpload() const noexcept { return has(DirectoryPermissions::Upload); }
    [[nodiscard]] bool canDownload() const noexcept { return has(DirectoryPermissions::Download); }
    [[nodiscard]] bool canTouch() const noexcept { return has(DirectoryPermissions::Touch); }
    [[nodiscard]] bool canDelete() const noexcept { return has(DirectoryPermissions::Delete); }
    [[nodiscard]] bool canRename() const noexcept { return has(DirectoryPermissions::Rename); }
    [[nodiscard]] bool canMove() const noexcept { return has(DirectoryPermissions::Move); }
    [[nodiscard]] bool canCopy() const noexcept { return has(DirectoryPermissions::Copy); }

    [[nodiscard]] bool canShareInternally() const noexcept { return share.has(SharePermissions::Internal); }
    [[nodiscard]] bool canSharePublicly() const noexcept { return share.has(SharePermissions::Public); }
    [[nodiscard]] bool canSharePubliclyWithVal() const noexcept { return share.has(SharePermissions::PublicWithValidation); }
};

void to_json(nlohmann::json& j, const Directories& v);
void from_json(const nlohmann::json& j, Directories& v);

}