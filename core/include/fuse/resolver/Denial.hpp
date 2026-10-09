#pragma once

#include "fs/Fwd.hpp"
#include "rbac/permission/vault/Filesystem.hpp"

#include <functional>
#include <memory>

namespace vh::fuse::resolver {
    using CanSeeEntry = std::function<bool(const std::shared_ptr<fs::model::Entry>&)>;

    // The errno a denied FUSE operation answers with (#170).
    //
    // A denial on something the caller cannot see (no Lookup on it, or for a create, on its parent) looks missing
    // (ENOENT), so the mount never confirms what exists. A denial on something the caller can see is EACCES.
    // The answer depends only on (caller, entry), never on which FUSE op reached the daemon first: the kernel's
    // dentry/attr cache is shared across uids, so a denied caller may skip the lookup and hit getattr, readdir or
    // open on an inode that another uid resolved, and that must not turn a hidden entry into EACCES.
    //
    // The subject is the entry when it exists, else the parent (create/mkdir/symlink of a new name). The mount root
    // is always visible. canSee is only consulted when the denied action does not already settle visibility.
    [[nodiscard]] int deniedErrno(
        rbac::permission::vault::FilesystemAction denied,
        const std::shared_ptr<fs::model::Entry>& entry,
        const std::shared_ptr<fs::model::Entry>& parentEntry,
        const CanSeeEntry& canSee
    );
}
