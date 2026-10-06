#include "common/sandbox.hpp"

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <linux/landlock.h>
#include <seccomp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace vh::helpers::sandbox {

namespace sandbox_detail {

uint64_t handledAccess(const int abi) {
    uint64_t access = (LANDLOCK_ACCESS_FS_MAKE_SYM << 1) - 1;  // ABI 1: EXECUTE .. MAKE_SYM
    if (abi >= 2) access |= LANDLOCK_ACCESS_FS_REFER;
    if (abi >= 3) access |= LANDLOCK_ACCESS_FS_TRUNCATE;
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
    if (abi >= 5) access |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
#endif
    return access;
}

constexpr uint64_t kReadOnly = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
constexpr uint64_t kFileRights = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
                                 LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_TRUNCATE
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
                                 | LANDLOCK_ACCESS_FS_IOCTL_DEV
#endif
    ;

void allowPath(const int ruleset, const std::string& path, uint64_t access, const uint64_t handled) {
    const int fd = ::open(path.c_str(), O_PATH | O_CLOEXEC);
    if (fd < 0) return;  // absent paths are simply not granted
    struct stat st{};
    if (::fstat(fd, &st) == 0 && !S_ISDIR(st.st_mode)) access &= kFileRights;
    landlock_path_beneath_attr attr{};
    attr.allowed_access = access & handled;
    attr.parent_fd = fd;
    const long rc = ::syscall(SYS_landlock_add_rule, ruleset, LANDLOCK_RULE_PATH_BENEATH, &attr, 0);
    const int err = errno;
    ::close(fd);
    if (rc != 0) throw std::system_error(err, std::generic_category(), "landlock_add_rule " + path);
}

bool applyLandlock(const Options& options, int& abiOut) {
    const long abi = ::syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 1) return false;
    abiOut = static_cast<int>(abi);
    const uint64_t handled = handledAccess(abiOut);

    landlock_ruleset_attr rulesetAttr{};
    rulesetAttr.handled_access_fs = handled;
    const long ruleset = ::syscall(SYS_landlock_create_ruleset, &rulesetAttr, sizeof(rulesetAttr), 0);
    if (ruleset < 0) throw std::system_error(errno, std::generic_category(), "landlock_create_ruleset");
    const int rs = static_cast<int>(ruleset);

    try {
        for (const char* p : {"/usr", "/lib", "/lib64", "/lib32", "/etc/ld.so.cache", "/etc/ld.so.conf",
                              "/etc/ld.so.conf.d", "/proc/self", "/dev/null", "/dev/urandom"})
            allowPath(rs, p, kReadOnly, handled);
        for (const auto& p : options.extraReadOnlyPaths) allowPath(rs, p, kReadOnly, handled);
        if (options.allowGpuDevices) {
            uint64_t gpu = kReadOnly | LANDLOCK_ACCESS_FS_WRITE_FILE;
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
            gpu |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
#endif
            allowPath(rs, "/dev/dri", gpu, handled);
            allowPath(rs, "/sys/dev/char", kReadOnly, handled);
            allowPath(rs, "/sys/devices", kReadOnly, handled);
        }
        if (::syscall(SYS_landlock_restrict_self, rs, 0) != 0)
            throw std::system_error(errno, std::generic_category(), "landlock_restrict_self");
    } catch (...) {
        ::close(rs);
        throw;
    }
    ::close(rs);
    return true;
}

void deny(scmp_filter_ctx ctx, const int syscallNr) {
    if (syscallNr < 0) return;  // not on this architecture
    if (const int rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscallNr, 0); rc != 0)
        throw std::system_error(-rc, std::generic_category(), "seccomp_rule_add");
}

void denyWriteOpen(scmp_filter_ctx ctx, const int syscallNr, const unsigned flagsArg) {
    if (syscallNr < 0) return;
    for (const scmp_datum_t mode : {scmp_datum_t{O_WRONLY}, scmp_datum_t{O_RDWR}}) {
        const scmp_arg_cmp cmp{flagsArg, SCMP_CMP_MASKED_EQ, O_ACCMODE, mode};
        if (const int rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscallNr, 1, cmp); rc != 0)
            throw std::system_error(-rc, std::generic_category(), "seccomp_rule_add");
    }
    const scmp_arg_cmp creat{flagsArg, SCMP_CMP_MASKED_EQ, O_CREAT, O_CREAT};
    if (const int rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), syscallNr, 1, creat); rc != 0)
        throw std::system_error(-rc, std::generic_category(), "seccomp_rule_add");
}

void applySeccomp(const Options& options) {
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (!ctx) throw std::runtime_error("seccomp_init failed");
    try {
        if (const int rc = seccomp_attr_set(ctx, SCMP_FLTATR_CTL_TSYNC, 1); rc != 0)
            throw std::system_error(-rc, std::generic_category(), "seccomp TSYNC");
        for (const char* name : {"socket", "connect", "bind", "listen", "accept", "accept4", "ptrace", "mount",
                                 "umount2", "unshare", "setns", "bpf", "keyctl", "add_key", "request_key",
                                 "kexec_load", "kexec_file_load", "execve", "execveat", "process_vm_readv",
                                 "process_vm_writev", "perf_event_open", "init_module", "finit_module",
                                 "delete_module", "pivot_root", "chroot", "open_by_handle_at", "userfaultfd",
                                 "io_uring_setup", "creat", "truncate", "rename", "renameat", "renameat2", "unlink",
                                 "unlinkat", "mkdir", "mkdirat", "rmdir", "link", "linkat", "symlink", "symlinkat"})
            deny(ctx, seccomp_syscall_resolve_name(name));
        // openat2 carries its flags in a struct seccomp cannot inspect: ENOSYS makes libc fall back to openat.
        if (const int nr = seccomp_syscall_resolve_name("openat2"); nr >= 0)
            if (const int rc = seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOSYS), nr, 0); rc != 0)
                throw std::system_error(-rc, std::generic_category(), "seccomp_rule_add openat2");
        if (!options.allowGpuDevices) {
            denyWriteOpen(ctx, seccomp_syscall_resolve_name("open"), 1);
            denyWriteOpen(ctx, seccomp_syscall_resolve_name("openat"), 2);
        } else {
            // GPU drivers open render nodes read-write; Landlock still confines writable paths to /dev/dri.
            const scmp_arg_cmp creatOpen{1, SCMP_CMP_MASKED_EQ, O_CREAT, O_CREAT};
            const scmp_arg_cmp creatOpenat{2, SCMP_CMP_MASKED_EQ, O_CREAT, O_CREAT};
            if (const int nr = seccomp_syscall_resolve_name("open"); nr >= 0)
                (void)seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), nr, 1, creatOpen);
            (void)seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), seccomp_syscall_resolve_name("openat"), 1, creatOpenat);
        }
        if (const int rc = seccomp_load(ctx); rc != 0) throw std::system_error(-rc, std::generic_category(), "seccomp_load");
    } catch (...) {
        seccomp_release(ctx);
        throw;
    }
    seccomp_release(ctx);
}

}

Report apply(const Options& options) {
    Report report;
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        throw std::system_error(errno, std::generic_category(), "PR_SET_NO_NEW_PRIVS");
    report.landlock = sandbox_detail::applyLandlock(options, report.landlockAbi);
    sandbox_detail::applySeccomp(options);
    report.seccomp = true;
    return report;
}

}
