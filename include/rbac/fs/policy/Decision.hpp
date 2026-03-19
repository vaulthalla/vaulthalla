#pragma once

#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/permission/Override.hpp"
#include "fs/model/Entry.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vh::rbac::fs::policy {
    struct Decision {
        enum class Reason : uint8_t {
            Allowed,
            MissingPath,
            MissingEntry,
            MissingPathAndEntry,
            MissingUser,
            InvalidActionForEntryType,
            DeniedByOverride,
            AllowedByOverride,
            DeniedByBasePermissions,
            AllowedByBasePermissions,
            StorageEngineNotFound
        };

        bool allowed{false};
        Reason reason{Reason::DeniedByBasePermissions};

        std::optional<std::string> evaluated_path{};
        std::optional<std::string> matched_override{};
        std::optional<permission::OverrideOpt> override_effect{};

        [[nodiscard]] std::string toString() const;
    };
}
