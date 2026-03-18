#pragma once

#include "rbac/permission/vault/Filesystem.hpp"
#include "fs/model/Entry.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace vh::rbac::fs::policy {

    struct Request {
        permission::vault::FilesystemAction action{};
        std::optional<std::filesystem::path> path{};
        std::optional<std::reference_wrapper<const vh::fs::model::Entry>> entry{};

        [[nodiscard]] bool hasEntry() const noexcept { return entry.has_value(); }
        [[nodiscard]] bool hasPath() const noexcept { return path.has_value(); }
    };

}
