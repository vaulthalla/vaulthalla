#pragma once

#include "rbac/permission/vault/Filesystem.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace vh::fs::model { struct Entry; }
namespace vh::identities { struct User; }

namespace vh::rbac::fs::policy {
    struct Request {
        std::shared_ptr<identities::User> user;
        permission::vault::FilesystemAction action{};
        std::optional<uint32_t> vaultId{};
        std::optional<std::filesystem::path> path{};
        std::shared_ptr<vh::fs::model::Entry> entry{};

        [[nodiscard]] bool hasEntry() const noexcept { return !!entry; }
        [[nodiscard]] bool hasPath() const noexcept { return path.has_value(); }
    };
}
