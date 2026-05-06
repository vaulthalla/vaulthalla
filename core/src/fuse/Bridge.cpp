#include "fuse/Bridge.hpp"
#include "storage/Manager.hpp"
#include "identities/User.hpp"
#include "fs/model/Entry.hpp"
#include "config/Registry.hpp"
#include "fs/Filesystem.hpp"
#include "runtime/Deps.hpp"
#include "stats/model/FuseStats.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/fs/Directory.hpp"
#include "log/Registry.hpp"
#include "fs/cache/Registry.hpp"
#include "fuse/Resolver.hpp"

#include <cerrno>
#include <cstring>
#include <sys/statvfs.h>
#include <unistd.h>

using namespace vh::identities;
using namespace vh::storage;
using namespace vh::config;
using namespace vh::fs;
using namespace vh::fs::model;
using namespace vh::rbac;

namespace vh::fuse {

namespace {

using stats::model::FuseOperation;
using stats::model::ScopedFuseOpTimer;

stats::model::FuseStats* fuseStats() noexcept {
    return runtime::Deps::get().fuseStats.get();
}

void replyError(const fuse_req_t req, ScopedFuseOpTimer& timer, const int errnum) {
    timer.error(errnum);
    fuse_reply_err(req, errnum);
}

void replyOk(const fuse_req_t req, ScopedFuseOpTimer& timer) {
    timer.success();
    fuse_reply_err(req, 0);
}

void recordOpenHandle() noexcept {
    if (const auto& stats = runtime::Deps::get().fuseStats) stats->record_open_handle();
}

void recordCloseHandle() noexcept {
    if (const auto& stats = runtime::Deps::get().fuseStats) stats->record_close_handle();
}

}

void getattr(const fuse_req_t req, const fuse_ino_t ino, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::GetAttr);
    log::Registry::fuse()->debug("[getattr] Called for inode: {}", ino);
    (void)fi;

    const auto resolved = Resolver::resolve({
        .caller = "getattr",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::List,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    const auto st = statFromEntry(resolved.entry, ino);
    timer.success();
    fuse_reply_attr(req, &st, 0.1); // match attr_timeout from lookup()
}

void setattr(const fuse_req_t req, const fuse_ino_t ino,
                         struct stat* attr, int to_set, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::SetAttr);
    (void)fi;
    log::Registry::fuse()->debug("[setattr] Called for inode: {}, to_set: {}", ino, to_set);

    if (to_set & FUSE_SET_ATTR_MODE) {
        log::Registry::fuse()->warn("⚔️ [Vaulthalla] Illegal access: chmod is forbidden beyond the gates!");
        replyError(req, timer, EPERM);
        return;
    }

    if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
        log::Registry::fuse()->warn("⚔️ [Vaulthalla] Illegal access: changing ownership is forbidden beyond the gates!");
        replyError(req, timer, EPERM);
        return;
    }

    const auto resolved = Resolver::resolve({
        .caller = "setattr",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::Write,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    timespec times[2];
    if (to_set & FUSE_SET_ATTR_ATIME) times[0] = attr->st_atim;
    else times[0].tv_nsec = UTIME_OMIT;
    if (to_set & FUSE_SET_ATTR_MTIME) times[1] = attr->st_mtim;
    else times[1].tv_nsec = UTIME_OMIT;

    if (::utimensat(AT_FDCWD, resolved.entry->backing_path.c_str(), times, 0) < 0) {
        replyError(req, timer, errno);
        return;
    }

    // Re-stat file so kernel gets fresh info
    struct stat st = {};
    if (::stat(resolved.entry->backing_path.c_str(), &st) < 0) {
        replyError(req, timer, errno);
        return;
    }

    timer.success();
    fuse_reply_attr(req, &st, 1.0);
}

void readdir(const fuse_req_t req, const fuse_ino_t ino, const size_t size, const off_t off, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::ReadDir);
    log::Registry::fuse()->debug("[readdir] Called for inode: {}, size: {}, offset: {}", ino, size, off);
    (void)fi;

    const std::optional<permission::vault::FilesystemAction> action = ino == FUSE_ROOT_ID ?
        std::nullopt : std::make_optional(permission::vault::FilesystemAction::List);

    const auto resolved = Resolver::resolve({
        .caller = "readdir",
        .fuseReq = req,
        .ino = ino,
        .action = action,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    const auto entries = runtime::Deps::get().fsCache->listDir(resolved.entry->id, false);

    std::vector<char> buf(size);
    size_t buf_used = 0;

    auto add_entry = [&](const std::string& name, const struct stat& st, const off_t next_off) {
        const size_t entry_size = fuse_add_direntry(req, nullptr, 0, name.c_str(), &st, next_off);
        if (buf_used + entry_size > size) return false;

        fuse_add_direntry(req, buf.data() + buf_used, entry_size, name.c_str(), &st, next_off);
        buf_used += entry_size;
        return true;
    };

    off_t current_off = 0;

    if (off <= current_off++) {
        struct stat dot{};
        dot.st_mode = S_IFDIR;
        if (!add_entry(".", dot, current_off)) goto reply;
    }

    if (off <= current_off++) {
        struct stat dotdot{};
        dotdot.st_mode = S_IFDIR;
        if (!add_entry("..", dotdot, current_off)) goto reply;
    }

    for (size_t i = 0; i < entries.size(); ++i, ++current_off) {
        if (off > current_off) continue;
        if (!add_entry(entries[i]->name, statFromEntry(entries[i], ino), current_off + 1)) break;
    }

    reply:
        timer.success(buf_used, 0);
        fuse_reply_buf(req, buf.data(), buf_used);
}

void lookup(const fuse_req_t req, const fuse_ino_t parent, const char* name) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Lookup);
    log::Registry::fuse()->debug("[lookup] Called for parent: {}, name: {}", parent, name);

    const auto resolved = Resolver::resolve({
        .caller = "lookup",
        .fuseReq = req,
        .parentIno = parent,
        .childName = name,
        .action = permission::vault::FilesystemAction::Lookup,
        .target = resolver::Target::EntryForPath
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    fuse_entry_param e{};
    e.ino = *resolved.ino;
    e.attr_timeout = 0.1;
    e.entry_timeout = 0.1;
    e.attr = statFromEntry(resolved.entry, *resolved.ino);

    timer.success();
    fuse_reply_entry(req, &e);
}

void create(const fuse_req_t req, const fuse_ino_t parent, const char* name, const mode_t mode, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Create);
    log::Registry::fuse()->debug("[create] Called for parent: {}, name: {}, mode: {}",
        parent, name, mode);

    const auto resolved = Resolver::resolve({
        .caller = "create",
        .fuseReq = req,
        .parentIno = parent,
        .childName = name,
        .action = permission::vault::FilesystemAction::Write,
        .target = resolver::Target::Path
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    if (runtime::Deps::get().fsCache->entryExists(*resolved.path)) {
        replyError(req, timer, EEXIST);
        return;
    }

    const auto [err, newEntry] = Filesystem::createFile(*resolved.path, getuid(), getgid(), mode);
    if (err) {
        replyError(req, timer, err);
        return;
    }

    const auto st = statFromEntry(newEntry, *newEntry->inode);

    // open backing file immediately
    const int fd = ::open(newEntry->backing_path.c_str(), O_CREAT | O_RDWR, mode);
    if (fd < 0) {
        replyError(req, timer, errno);
        return;
    }

    auto* fh = new FileHandle{newEntry->backing_path.string(), fd};
    fi->fh = reinterpret_cast<uint64_t>(fh);

    fi->direct_io = 1;
    fi->keep_cache = 0;

    fuse_entry_param e{};
    e.ino           = *newEntry->inode;
    e.attr          = st;
    e.attr_timeout  = 60.0;
    e.entry_timeout = 60.0;

    runtime::Deps::get().storageManager->registerOpenHandle(*newEntry->inode);
    recordOpenHandle();
    timer.success();
    fuse_reply_create(req, &e, fi);
}

void open(const fuse_req_t req, const fuse_ino_t ino, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Open);
    log::Registry::fuse()->debug("[open] Called for inode: {}, flags: {}", ino, fi->flags);

    const auto resolved = Resolver::resolve({
        .caller = "open",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::Read,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    const int fd = ::open(resolved.entry->backing_path.c_str(), fi->flags, 0644);
    if (fd < 0) {
        replyError(req, timer, errno);
        return;
    }

    auto* fh = new FileHandle{resolved.entry->backing_path.string(), fd};
    fi->fh = reinterpret_cast<uint64_t>(fh);

    fi->direct_io = 1;
    fi->keep_cache = 0;

    runtime::Deps::get().storageManager->registerOpenHandle(ino);
    recordOpenHandle();
    timer.success();
    fuse_reply_open(req, fi);
}

void write(const fuse_req_t req, const fuse_ino_t ino, const char* buf,
                       const size_t size, const off_t off, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Write);
    log::Registry::fuse()->debug("[write] Called for inode: {}, size: {}, offset: {}, file handle: {}",
        ino, size, off, fi->fh);

    const auto* fh = reinterpret_cast<FileHandle*>(fi->fh);
    if (!fh) {
        log::Registry::fuse()->debug("[write] Invalid file handle for inode: {}", ino);
        replyError(req, timer, EBADF);
        return;
    }

    log::Registry::fuse()->debug("[write] Writing to fd={} offset={} size={}", fh->fd, off, size);

    const ssize_t res = ::pwrite(fh->fd, buf, size, off);
    if (res < 0) {
        replyError(req, timer, errno);
        return;
    }

    const auto resolved = Resolver::resolve({
        .caller = "write",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::Write,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    resolved.entry->size_bytes = std::filesystem::file_size(resolved.entry->backing_path);
    runtime::Deps::get().fsCache->updateEntry(resolved.entry);

    fuse_lowlevel_notify_inval_inode(runtime::Deps::get().fuseSession, ino, 0, 0);
    timer.success(0, static_cast<std::uint64_t>(res));
    fuse_reply_write(req, res);
}

void read(const fuse_req_t req, const fuse_ino_t ino, const size_t size, const off_t off, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Read);
    log::Registry::fuse()->debug("[read] Called for inode: {}, size: {}, offset: {}, file handle: {}",
        ino, size, off, fi->fh);

    const auto* fh = reinterpret_cast<FileHandle*>(fi->fh);
    if (!fh) {
        log::Registry::fuse()->debug("[read] Invalid file handle for inode: {}", ino);
        replyError(req, timer, EBADF);
        return;
    }

    const auto resolved = Resolver::resolve({
        .caller = "read",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::Read,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    std::vector<char> buffer(size);
    const ssize_t res = ::pread(fh->fd, buffer.data(), size, off);

    if (res < 0) {
        replyError(req, timer, errno);
        return;
    }

    timer.success(static_cast<std::uint64_t>(res), 0);
    fuse_reply_buf(req, buffer.data(), res);
}

void mkdir(const fuse_req_t req, const fuse_ino_t parent, const char* name, const mode_t mode) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::MkDir);
    log::Registry::fuse()->debug("[mkdir] Called for parent: {}, name: {}, mode: {}", parent, name, mode);

    const auto resolved = Resolver::resolve({
        .caller = "mkdir",
        .fuseReq = req,
        .parentIno = parent,
        .childName = name,
        .action = permission::vault::FilesystemAction::Touch,
        .target = resolver::Target::Path
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    if (std::string_view(name).find('/') != std::string::npos) {
        replyError(req, timer, EINVAL);
        return;
    }

    if (const auto err = Filesystem::mkdir(*resolved.path, mode); err) {
        replyError(req, timer, err);
        return;
    }

    const auto finalInode = runtime::Deps::get().fsCache->resolveInode(*resolved.path);
    const auto finalEntry = runtime::Deps::get().fsCache->getEntry(*resolved.path);

    fuse_entry_param e{};
    e.ino = finalInode;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    e.attr = statFromEntry(finalEntry, finalInode);

    timer.success();
    fuse_reply_entry(req, &e);
}

void rename(const fuse_req_t req, const fuse_ino_t parent, const char* name, const fuse_ino_t newparent, const char* newname, const unsigned int flags) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Rename);
    log::Registry::fuse()->debug("[rename] Called for parent: {}, name: {}, newparent: {}, newname: {}, flags: {}",
                               parent, name, newparent, newname, flags);

    const auto& cache = runtime::Deps::get().fsCache;

    const auto toPath = cache->resolvePath(newparent) / newname;

    const auto resolved = Resolver::resolve({
        .caller = "rename",
        .fuseReq = req,
        .parentIno = parent,
        .childName = name,
        .action = permission::vault::FilesystemAction::Rename,
        .target = resolver::Target::EntryForPath
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    // Flags handling (RENAME_NOREPLACE = 1, RENAME_EXCHANGE = 2)
    if ((flags & RENAME_NOREPLACE) && cache->entryExists(toPath)) {
        replyError(req, timer, EEXIST);
        return;
    }

    if (const auto err = Filesystem::rename(*resolved.path, toPath); err) {
        replyError(req, timer, err);
        return;
    }

    replyOk(req, timer);
}

void forget(const fuse_req_t req, const fuse_ino_t ino, const uint64_t nlookup) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Forget);
    log::Registry::fuse()->debug("[forget] Called for inode: {}, nlookup: {}", ino, nlookup);

    const auto resolved = Resolver::resolve({
        .caller = "forget",
        .fuseReq = req,
        .ino = ino,
        .target = resolver::Target::EntryForPath
    });

    if (!resolved.ok()) {
        log::Registry::fuse()->debug("[forget] No entry found for inode {}: {}", ino, resolved.errnum);
        timer.success();
        fuse_reply_none(req); // still need to reply to avoid hanging the kernel, even if we have nothing to evict
        return;
    }

    runtime::Deps::get().fsCache->evictIno(ino);

    log::Registry::fuse()->debug("[forget] Evicted inode {}", ino);
    timer.success();
    fuse_reply_none(req); // no return value
}

void access(const fuse_req_t req, const fuse_ino_t ino, const int mask) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Access);
    log::Registry::fuse()->debug("[access] Called for inode: {}, mask: {}", ino, mask);

    std::vector<rbac::permission::vault::FilesystemAction> requiredPermissions;
    if (mask & W_OK) requiredPermissions.push_back(permission::vault::FilesystemAction::Write);
    if (mask & R_OK) requiredPermissions.push_back(permission::vault::FilesystemAction::Read);
    if (ino != FUSE_ROOT_ID && mask & X_OK) requiredPermissions.push_back(permission::vault::FilesystemAction::List);

    const auto resolved = Resolver::resolve({
        .caller = "access",
        .fuseReq = req,
        .ino = ino,
        .actions = requiredPermissions,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    replyOk(req, timer);
}

void unlink(const fuse_req_t req, const fuse_ino_t parent, const char* name) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Unlink);
    log::Registry::fuse()->debug("[unlink] Called for parent: {}, name: {}", parent, name);

    const auto resolved = Resolver::resolve({
        .caller = "unlink",
        .fuseReq = req,
        .parentIno = parent,
        .childName = name,
        .action = permission::vault::FilesystemAction::Delete,
        .target = resolver::Target::EntryForPath
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    if (resolved.entry->isDirectory()) {
        replyError(req, timer, EISDIR);
        return;
    }

    db::query::fs::File::markFileAsTrashed(resolved.user->id, *resolved.entry->vault_id, resolved.entry->path, true);

    if (::unlink(resolved.entry->backing_path.c_str()) < 0)
        log::Registry::fuse()->debug("[unlink] Failed to remove backing file: {}: {}", resolved.entry->backing_path.string(), strerror(errno));

    replyOk(req, timer);
}

void rmdir(const fuse_req_t req, const fuse_ino_t parent, const char* name) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::RmDir);
    log::Registry::fuse()->debug("[rmdir] Called for parent: {}, name: {}", parent, name);

    const auto resolved = Resolver::resolve({
        .caller = "rmdir",
        .fuseReq = req,
        .parentIno = parent,
        .childName = name,
        .action = permission::vault::FilesystemAction::Delete,
        .target = resolver::Target::EntryForPath
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    db::query::fs::Directory::deleteEmptyDirectory(resolved.entry->id);

    if (::rmdir(resolved.entry->backing_path.c_str()) < 0)
        log::Registry::fuse()->warn("[rmdir] Failed to remove backing directory: {}: {}", resolved.entry->backing_path.string(), strerror(errno));

    replyOk(req, timer);
}

void flush(const fuse_req_t req, const fuse_ino_t ino, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Flush);
    log::Registry::fuse()->debug("[flush] Called for inode: {}, file handle: {}", ino, fi->fh);

    // This can be a no-op unless something like sync(2) or call fsync(fd) is desired
    // Likely no need finalize here since flush may be called multiple times per FD

    replyOk(req, timer);
}

void release(const fuse_req_t req, const fuse_ino_t ino, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Release);
    log::Registry::fuse()->debug("[release] Called for inode: {}, file handle: {}", ino, fi->fh);

    const auto* fh = reinterpret_cast<FileHandle*>(fi->fh);
    if (!fh) {
        log::Registry::fuse()->debug("[release] Invalid file handle for inode: {}", ino);
        replyError(req, timer, EBADF);
        return;
    }

    if (::close(fh->fd) < 0)
        log::Registry::fuse()->debug("[release] Failed to close file handle: {}: {}", fh->path.string(), strerror(errno));

    delete fh;  // clean up heap allocation
    fi->fh = 0; // clear the kernel-side handle

    runtime::Deps::get().storageManager->closeOpenHandle(ino);
    recordCloseHandle();

    replyOk(req, timer);
}

void fsync(const fuse_req_t req, const fuse_ino_t ino, const int datasync, fuse_file_info* fi) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::Fsync);
    log::Registry::fuse()->debug("[fsync] Called for inode: {}, file handle: {}, isdatasync: {}", ino, fi->fh, datasync);
    (void) datasync;

    const auto resolved = Resolver::resolve({
        .caller = "fsync",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::Write,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    const auto* fh = reinterpret_cast<FileHandle*>(fi->fh);
    if (!fh) {
        log::Registry::fuse()->debug("[fsync] Invalid file handle for inode: {}", ino);
        replyError(req, timer, EBADF);
        return;
    }

    if (::fsync(fh->fd) < 0) {
        log::Registry::fuse()->debug("[fsync] Failed to sync file handle: {}: {}", fh->fd, strerror(errno));
        replyError(req, timer, errno);
        return;
    }

    replyOk(req, timer);
}

void statfs(const fuse_req_t req, const fuse_ino_t ino) {
    ScopedFuseOpTimer timer(fuseStats(), FuseOperation::StatFs);
    log::Registry::fuse()->debug("[statfs] Called for inode: {}", ino);

    const auto resolved = Resolver::resolve({
        .caller = "statfs",
        .fuseReq = req,
        .ino = ino,
        .action = permission::vault::FilesystemAction::Read,
        .target = resolver::Target::Entry
    });

    if (!resolved.ok()) {
        replyError(req, timer, resolved.errnum);
        return;
    }

    struct statvfs st{};

    if (::statvfs(resolved.entry->backing_path.c_str(), &st) < 0) {
        log::Registry::fuse()->debug("[statfs] Failed to get filesystem stats for: {}: {}", resolved.entry->backing_path.string(), strerror(errno));
        replyError(req, timer, errno);
        return;
    }

    timer.success();
    fuse_reply_statfs(req, &st);
}

fuse_lowlevel_ops getOperations() {
    fuse_lowlevel_ops ops = {};
    ops.getattr = getattr;
    ops.setattr = setattr;
    ops.readdir = readdir;
    ops.lookup = lookup;
    ops.open = open;
    ops.read = read;
    ops.forget = forget;
    ops.write = write;
    ops.create = create;
    ops.release = release;
    ops.access = access;
    ops.mkdir = mkdir;
    ops.rename = rename;
    ops.unlink = unlink;
    ops.rmdir = rmdir;
    ops.flush = flush;
    ops.fsync = fsync;
    ops.statfs = statfs;
    return ops;
}

struct stat statFromEntry(const std::shared_ptr<Entry>& entry, const fuse_ino_t& ino) {
    struct stat st{};
    st.st_ino = ino;
    st.st_mode = entry->isDirectory() ? S_IFDIR | 0755 : S_IFREG | 0644;
    st.st_size = static_cast<off_t>(entry->size_bytes);
    st.st_mtim.tv_sec = entry->updated_at;
    st.st_atim = st.st_ctim = st.st_mtim;
    st.st_nlink = 1;
    return st;
}

}
