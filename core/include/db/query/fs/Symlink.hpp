#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vh::fs::model { struct Symlink; }

namespace vh::db::query::fs {

class Symlink {
    using S = vh::fs::model::Symlink;
    using SymlinkPtr = std::shared_ptr<S>;

public:
    Symlink() = default;

    static unsigned int upsertSymlink(const SymlinkPtr& symlink);

    static void deleteSymlink(const SymlinkPtr& symlink);

    static SymlinkPtr getSymlinkById(unsigned int id);

    static SymlinkPtr getSymlinkByPath(unsigned int vaultId, const std::filesystem::path& relPath);

    static SymlinkPtr getSymlinkByInode(ino_t ino);

    static std::vector<SymlinkPtr> listSymlinksInDir(unsigned int parentId, bool recursive = false);
};

}
