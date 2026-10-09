// #170: a denied FUSE op answers ENOENT when the caller cannot see the target and EACCES when it can, whichever op
// reaches the daemon first. The kernel's dentry/attr cache is shared across uids, so a caller with no vault access
// can skip LOOKUP entirely and land on getattr/readdir/open for an inode another uid resolved; `ls seed` used to
// answer EACCES that way while every lookup-first path answered ENOENT.

#include "fs/model/Directory.hpp"
#include "fs/model/File.hpp"
#include "fuse/resolver/Denial.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <memory>
#include <vector>

namespace vh::test_fuse_denial {

using Action = rbac::permission::vault::FilesystemAction;
using EntryPtr = std::shared_ptr<fs::model::Entry>;

EntryPtr mountRoot() {
    auto d = std::make_shared<fs::model::Directory>();
    d->path = "/";
    d->fuse_path = "/";
    return d;
}

EntryPtr vaultRoot() {
    auto d = std::make_shared<fs::model::Directory>();
    d->vault_id = 7;
    d->path = "/";
    d->fuse_path = "/vault";
    return d;
}

EntryPtr directory(const char* vaultPath) {
    auto d = std::make_shared<fs::model::Directory>();
    d->vault_id = 7;
    d->path = vaultPath;
    d->fuse_path = std::filesystem::path("/vault") / std::filesystem::path(vaultPath).relative_path();
    return d;
}

EntryPtr file(const char* vaultPath) {
    auto f = std::make_shared<fs::model::File>();
    f->vault_id = 7;
    f->path = vaultPath;
    f->fuse_path = std::filesystem::path("/vault") / std::filesystem::path(vaultPath).relative_path();
    return f;
}

struct Probe {
    bool visible{};
    std::vector<EntryPtr> asked{};

    fuse::resolver::CanSeeEntry fn() {
        return [this](const EntryPtr& e) {
            asked.push_back(e);
            return visible;
        };
    }
};

// Every FUSE op the daemon can see first for a path (lookup, getattr, readdir, open, setattr, unlink...).
const std::vector<Action> kOps{
    Action::Lookup, Action::List, Action::Read, Action::Write, Action::Delete, Action::Rename, Action::Touch
};

TEST(FuseDenial, HiddenEntryLooksMissingWhicheverOpArrivesFirst) {
    for (const auto& entry : {directory("/perm_deny_seed"), file("/perm_deny_seed/note.txt"), vaultRoot()}) {
        for (const auto action : kOps) {
            Probe hidden{.visible = false};
            EXPECT_EQ(fuse::resolver::deniedErrno(action, entry, vaultRoot(), hidden.fn()), ENOENT)
                << entry->fuse_path << " action " << static_cast<int>(action);
        }
    }
}

TEST(FuseDenial, ListingADirectoryNobodyShowedTheCallerIsEnoent) {
    // The `ls seed` case: the kernel served seed's dentry/attrs from cache, so the daemon first sees readdir.
    const auto seed = directory("/perm_deny_seed");
    Probe hidden{.visible = false};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::List, seed, nullptr, hidden.fn()), ENOENT);
    ASSERT_EQ(hidden.asked.size(), 1u);
    EXPECT_EQ(hidden.asked.front(), seed);
}

TEST(FuseDenial, VisibleEntryMissingTheActionIsEacces) {
    // Can see seed (e.g. traversal toward an allowed override) but not list it; can see a file but not read it.
    Probe visible{.visible = true};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::List, directory("/seed"), nullptr, visible.fn()), EACCES);
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Read, file("/seed/docs/secret.txt"), nullptr, visible.fn()), EACCES);
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Write, file("/seed/note.txt"), nullptr, visible.fn()), EACCES);
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Delete, directory("/seed"), vaultRoot(), visible.fn()), EACCES);
}

TEST(FuseDenial, DeniedLookupIsHiddenWithoutReevaluating) {
    Probe visible{.visible = true};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Lookup, file("/seed/note.txt"), nullptr, visible.fn()), ENOENT);
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Lookup, vaultRoot(), mountRoot(), visible.fn()), ENOENT);
    EXPECT_TRUE(visible.asked.empty());
}

TEST(FuseDenial, CreatingANewNameJudgesTheParent) {
    // create/mkdir/symlink of a name that does not exist: no entry, so the parent decides.
    const auto docs = directory("/seed/docs");

    Probe hidden{.visible = false};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Write, nullptr, docs, hidden.fn()), ENOENT);
    ASSERT_EQ(hidden.asked.size(), 1u);
    EXPECT_EQ(hidden.asked.front(), docs);

    Probe visible{.visible = true};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Touch, nullptr, docs, visible.fn()), EACCES);
}

TEST(FuseDenial, MountRootIsNeverHidden) {
    Probe hidden{.visible = false};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Write, mountRoot(), nullptr, hidden.fn()), EACCES);
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Touch, nullptr, mountRoot(), hidden.fn()), EACCES);
    EXPECT_TRUE(hidden.asked.empty());
}

TEST(FuseDenial, NothingToJudgeIsEacces) {
    Probe hidden{.visible = false};
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::Write, nullptr, nullptr, hidden.fn()), EACCES);
    EXPECT_EQ(fuse::resolver::deniedErrno(Action::List, directory("/seed"), nullptr, nullptr), ENOENT)
        << "no visibility probe: hide rather than confirm";
}

}
