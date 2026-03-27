#pragma once

#include "rbac/permission/vault/Filesystem.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace vh::fs::model { struct Entry; }
namespace vh::identities { struct User; }

namespace vh::rbac::fs::policy {
    enum class ThreatLevel : uint8_t {
        Low = 1,
        Medium = 2,
        High = 3,
        Critical = 4
    };

    struct Request {
        std::shared_ptr<identities::User> user;
        permission::vault::FilesystemAction action{};
        ThreatLevel threatLevel{ ThreatLevel::High };
        std::optional<uint32_t> vaultId{};
        std::optional<std::filesystem::path> path{};
        std::shared_ptr<vh::fs::model::Entry> entry{};

        [[nodiscard]] bool hasEntry() const noexcept { return !!entry; }
        [[nodiscard]] bool hasPath() const noexcept { return path.has_value(); }
    };
}
