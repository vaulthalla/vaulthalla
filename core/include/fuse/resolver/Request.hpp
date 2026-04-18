#pragma once

#define FUSE_USE_VERSION 35

#include "rbac/permission/vault/Filesystem.hpp"

#include <fuse_lowlevel.h>
#include <optional>
#include <string>
#include <cstdint>
#include <vector>

namespace vh::fuse::resolver {
    enum class Target : uint32_t {
        None = 0,
        Entry = 1 << 0,
        Path = 1 << 1,
        EntryForPath = 1 << 2,
    };

    inline constexpr Target operator|(const Target lhs, const Target rhs) {
        return static_cast<Target>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)
        );
    }

    inline constexpr Target operator&(const Target lhs, const Target rhs) {
        return static_cast<Target>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)
        );
    }

    inline constexpr bool hasFlag(const Target value, const Target flag) {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    struct Request {
        std::string caller;
        fuse_req_t fuseReq;

        std::optional<fuse_ino_t> ino{}, parentIno{};
        std::optional<std::string> childName{};

        std::optional<rbac::permission::vault::FilesystemAction> action{};
        std::vector<rbac::permission::vault::FilesystemAction> actions{};

        Target target {Target::Entry};
    };
}
