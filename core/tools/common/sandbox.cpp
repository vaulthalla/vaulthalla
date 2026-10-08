#include "sandbox.hpp"

#include <linux/landlock.h>
#include <sched.h>
#include <seccomp.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace vh::helpers::sandbox {

namespace {

// Newer than the uapi header on the build host (Ubuntu noble ships ABI 4); the kernel tells us what it supports.
constexpr uint64_t kAccessFsIoctlDev = 1ULL << 15;            // ABI 5
constexpr uint64_t kScopeAbstractUnixSocket = 1ULL << 0;      // ABI 6
constexpr uint64_t kScopeSignal = 1ULL << 1;                  // ABI 6

// Layout of struct landlock_ruleset_attr across ABIs (the kernel accepts the prefix it knows).
struct RulesetAttr {
    uint64_t handledAccessFs;
    uint64_t handledAccessNet;
    uint64_t scoped;
};

constexpr uint64_t kFsReadFile = LANDLOCK_ACCESS_FS_READ_FILE;
constexpr uint64_t kFsReadDir = LANDLOCK_ACCESS_FS_READ_DIR;
constexpr uint64_t kFsExecute = LANDLOCK_ACCESS_FS_EXECUTE;
constexpr uint64_t kFsWriteFile = LANDLOCK_ACCESS_FS_WRITE_FILE;

// Rights that may be granted on a non-directory.
constexpr uint64_t kFileOnlyRights = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
                                     LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_TRUNCATE | kAccessFsIoctlDev;

uint64_t handledFsFor(const int abi) {
    uint64_t fs = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_READ_FILE |
                  LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
                  LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
                  LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
                  LANDLOCK_ACCESS_FS_MAKE_SYM;
    if (abi >= 2) fs |= LANDLOCK_ACCESS_FS_REFER;
    if (abi >= 3) fs |= LANDLOCK_ACCESS_FS_TRUNCATE;
    if (abi >= 5) fs |= kAccessFsIoctlDev;
    return fs;
}

int landlockAbi() {
    const long abi = ::syscall(SYS_landlock_create_ruleset, nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    return abi < 0 ? 0 : static_cast<int>(abi);
}

class Fd {
public:
    explicit Fd(const int fd) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    [[nodiscard]] int get() const { return fd_; }
private:
    int fd_;
};

// Adds a path-beneath rule; a path that does not exist on this host is skipped (not an error).
bool allowPath(const int rulesetFd, const std::string& path, uint64_t access, const uint64_t handled,
               std::string& detail) {
    const Fd fd(::open(path.c_str(), O_PATH | O_CLOEXEC));
    if (fd.get() < 0) return errno == ENOENT || errno == ENOTDIR || errno == EACCES;

    struct stat st{};
    if (::fstat(fd.get(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) access &= kFileOnlyRights;
    access &= handled;
    if (access == 0) return true;

    landlock_path_beneath_attr rule{};
    rule.allowed_access = access;
    rule.parent_fd = fd.get();
    if (::syscall(SYS_landlock_add_rule, rulesetFd, LANDLOCK_RULE_PATH_BENEATH, &rule, 0) != 0) {
        detail += "landlock_add_rule(" + path + "): " + std::strerror(errno) + "; ";
        return false;
    }
    return true;
}

std::vector<std::string> gpuDevicePaths() {
    std::vector<std::string> paths{"/dev/dri"};
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("nvidia")) paths.push_back(entry.path().string());
    }
    return paths;
}

bool applyLandlock(const Options& options, const int abi, std::string& detail) {
    const uint64_t handledFs = handledFsFor(abi);
    RulesetAttr attr{handledFs, 0, 0};
    std::size_t attrSize = sizeof(uint64_t);
    if (abi >= 4) {
        attr.handledAccessNet = LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP;
        attrSize = 2 * sizeof(uint64_t);
    }
    if (abi >= 6) {
        attr.scoped = kScopeAbstractUnixSocket | kScopeSignal;
        attrSize = sizeof(RulesetAttr);
    }

    const Fd ruleset(static_cast<int>(::syscall(SYS_landlock_create_ruleset, &attr, attrSize, 0)));
    if (ruleset.get() < 0) {
        detail += std::string("landlock_create_ruleset: ") + std::strerror(errno) + "; ";
        return false;
    }

    constexpr uint64_t readOnly = kFsReadFile | kFsReadDir | kFsExecute;
    std::vector<std::string> readOnlyPaths{
        "/usr", "/lib", "/lib64", "/lib32", "/etc/ld.so.cache", "/etc/ld.so.conf", "/etc/ld.so.conf.d",
        "/proc/self", "/sys/devices/system/cpu", "/dev/null", "/dev/urandom",
    };
    readOnlyPaths.insert(readOnlyPaths.end(), options.extraReadOnlyPaths.begin(), options.extraReadOnlyPaths.end());

    bool ok = true;
    for (const auto& path : readOnlyPaths) ok = allowPath(ruleset.get(), path, readOnly, handledFs, detail) && ok;
    if (options.allowGpuDevices) {
        for (const auto& path : gpuDevicePaths())
            ok = allowPath(ruleset.get(), path, kFsReadFile | kFsReadDir | kFsWriteFile | kAccessFsIoctlDev,
                           handledFs, detail) && ok;
        // libdrm / VA-API / NVENC discover devices through sysfs (and the NVIDIA driver through procfs).
        for (const char* path : {"/sys/dev/char", "/sys/devices", "/proc/driver/nvidia"})
            ok = allowPath(ruleset.get(), path, readOnly, handledFs, detail) && ok;
    }
    if (!ok) return false;

    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        detail += std::string("PR_SET_NO_NEW_PRIVS: ") + std::strerror(errno) + "; ";
        return false;
    }
    if (::syscall(SYS_landlock_restrict_self, ruleset.get(), 0) != 0) {
        detail += std::string("landlock_restrict_self: ") + std::strerror(errno) + "; ";
        return false;
    }
    return true;
}

class SeccompFilter {
public:
    SeccompFilter() : ctx_(seccomp_init(SCMP_ACT_ALLOW)) {}
    ~SeccompFilter() { if (ctx_) seccomp_release(ctx_); }
    SeccompFilter(const SeccompFilter&) = delete;
    SeccompFilter& operator=(const SeccompFilter&) = delete;

    [[nodiscard]] bool valid() const { return ctx_ != nullptr && failures_.empty(); }
    [[nodiscard]] const std::string& failures() const { return failures_; }
    [[nodiscard]] scmp_filter_ctx get() const { return ctx_; }

    void attr(const scmp_filter_attr attribute, const uint32_t value, const char* name) {
        if (ctx_ && seccomp_attr_set(ctx_, attribute, value) != 0) failures_ += std::string("attr ") + name + "; ";
    }

    void deny(const int syscall, const int err, const char* name) {
        if (ctx_ && seccomp_rule_add(ctx_, SCMP_ACT_ERRNO(static_cast<uint32_t>(err)), syscall, 0) != 0)
            failures_ += std::string(name) + "; ";
    }

    // Kernel ABI for an `int`/`unsigned int` argument (fcntl/ioctl commands): only the low 32 bits count, so an
    // equality rule must ignore whatever the caller put in the upper half.
    void denyIfLow32Eq(const int syscall, const int err, const unsigned arg, const uint64_t value, const char* name) {
        const scmp_arg_cmp cmp{arg, SCMP_CMP_MASKED_EQ, 0xffffffffULL, value};
        denyIf(syscall, err, cmp, name);
    }

    void denyIf(const int syscall, const int err, const scmp_arg_cmp cmp, const char* name) {
        if (ctx_ && seccomp_rule_add(ctx_, SCMP_ACT_ERRNO(static_cast<uint32_t>(err)), syscall, 1, cmp) != 0)
            failures_ += std::string(name) + "; ";
    }

private:
    scmp_filter_ctx ctx_;
    std::string failures_;
};

bool applySeccomp(const bool denyOpenForWrite, const bool allowThreads, std::string& detail) {
    SeccompFilter f;
    if (!f.get()) {
        detail += "seccomp_init failed; ";
        return false;
    }
    f.attr(SCMP_FLTATR_ACT_BADARCH, SCMP_ACT_KILL_PROCESS, "badarch");
    f.attr(SCMP_FLTATR_CTL_NNP, 1, "nnp");
    f.attr(SCMP_FLTATR_CTL_TSYNC, 1, "tsync");

#define VH_DENY(name) f.deny(SCMP_SYS(name), EPERM, #name)
    // Network and IPC endpoints: the only channel is the inherited fd 3.
    VH_DENY(socket); VH_DENY(socketpair); VH_DENY(connect); VH_DENY(bind); VH_DENY(listen);
    VH_DENY(accept); VH_DENY(accept4);
    f.denyIf(SCMP_SYS(sendto), EPERM, SCMP_A4(SCMP_CMP_NE, 0), "sendto-addr");
    // No new programs, no new processes (threads only), no namespaces.
    VH_DENY(execve); VH_DENY(execveat); VH_DENY(fork); VH_DENY(vfork);
    if (allowThreads) {
        f.denyIf(SCMP_SYS(clone), EPERM, SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_THREAD, 0), "clone-process");
        for (const uint64_t ns : {static_cast<uint64_t>(CLONE_NEWNS), static_cast<uint64_t>(CLONE_NEWUSER),
                                  static_cast<uint64_t>(CLONE_NEWPID), static_cast<uint64_t>(CLONE_NEWNET),
                                  static_cast<uint64_t>(CLONE_NEWIPC), static_cast<uint64_t>(CLONE_NEWUTS),
                                  static_cast<uint64_t>(CLONE_NEWCGROUP)})
            f.denyIf(SCMP_SYS(clone), EPERM, SCMP_A0(SCMP_CMP_MASKED_EQ, ns, ns), "clone-namespace");
    } else {
        f.deny(SCMP_SYS(clone), EAGAIN, "clone");   // what pthread_create reports when it may not start a thread
    }
    f.deny(SCMP_SYS(clone3), ENOSYS, "clone3");
    VH_DENY(unshare); VH_DENY(setns);
    // Other processes (the daemon runs as the same user).
    VH_DENY(ptrace); VH_DENY(process_vm_readv); VH_DENY(process_vm_writev);
    VH_DENY(pidfd_open); VH_DENY(pidfd_getfd); VH_DENY(pidfd_send_signal); VH_DENY(tkill);
    VH_DENY(kcmp);
    const auto self = static_cast<scmp_datum_t>(::getpid());
    f.denyIf(SCMP_SYS(kill), EPERM, SCMP_A0(SCMP_CMP_NE, self), "kill-other");
    f.denyIf(SCMP_SYS(tgkill), EPERM, SCMP_A0(SCMP_CMP_NE, self), "tgkill-other");
    f.denyIf(SCMP_SYS(rt_sigqueueinfo), EPERM, SCMP_A0(SCMP_CMP_NE, self), "rt_sigqueueinfo-other");
    f.denyIf(SCMP_SYS(rt_tgsigqueueinfo), EPERM, SCMP_A0(SCMP_CMP_NE, self), "rt_tgsigqueueinfo-other");
    // Resource and scheduling knobs of other processes (same uid: the kernel would allow them). pid 0 = self; a
    // non-zero upper half fails the comparison, so these rules fail closed.
    f.denyIf(SCMP_SYS(prlimit64), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "prlimit64-other");
    f.denyIf(SCMP_SYS(setpriority), EPERM, SCMP_A0(SCMP_CMP_NE, PRIO_PROCESS), "setpriority-which");
    f.denyIf(SCMP_SYS(setpriority), EPERM, SCMP_A1(SCMP_CMP_NE, 0), "setpriority-other");
    constexpr scmp_datum_t kIoprioWhoProcess = 1;
    f.denyIf(SCMP_SYS(ioprio_set), EPERM, SCMP_A0(SCMP_CMP_NE, kIoprioWhoProcess), "ioprio_set-which");
    f.denyIf(SCMP_SYS(ioprio_set), EPERM, SCMP_A1(SCMP_CMP_NE, 0), "ioprio_set-other");
    f.denyIf(SCMP_SYS(sched_setaffinity), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "sched_setaffinity-other");
    f.denyIf(SCMP_SYS(sched_setscheduler), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "sched_setscheduler-other");
    f.denyIf(SCMP_SYS(sched_setparam), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "sched_setparam-other");
    f.denyIf(SCMP_SYS(sched_setattr), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "sched_setattr-other");
    f.denyIf(SCMP_SYS(migrate_pages), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "migrate_pages-other");
    f.denyIf(SCMP_SYS(move_pages), EPERM, SCMP_A0(SCMP_CMP_NE, 0), "move_pages-other");
    VH_DENY(process_madvise); VH_DENY(process_mrelease);
    // No fd may be armed to signal another process (F_SETOWN to the daemon + F_SETSIG SIGKILL + O_ASYNC), and no
    // lease may stall another process's open.
    for (const auto& [cmd, name] : {std::pair<uint64_t, const char*>{F_SETOWN, "fcntl-F_SETOWN"},
                                    {F_SETOWN_EX, "fcntl-F_SETOWN_EX"}, {F_SETSIG, "fcntl-F_SETSIG"},
                                    {F_SETLEASE, "fcntl-F_SETLEASE"}})
        f.denyIfLow32Eq(SCMP_SYS(fcntl), EPERM, 1, cmd, name);
    f.denyIfLow32Eq(SCMP_SYS(ioctl), EPERM, 1, FIOSETOWN, "ioctl-FIOSETOWN");
    f.denyIfLow32Eq(SCMP_SYS(ioctl), EPERM, 1, SIOCSPGRP, "ioctl-SIOCSPGRP");
    // Kernel objects that would outlive the helper: SysV IPC and POSIX message queues.
    VH_DENY(shmget); VH_DENY(shmat); VH_DENY(shmdt); VH_DENY(shmctl);
    VH_DENY(msgget); VH_DENY(msgsnd); VH_DENY(msgrcv); VH_DENY(msgctl);
    VH_DENY(semget); VH_DENY(semop); VH_DENY(semtimedop); VH_DENY(semctl);
    VH_DENY(mq_open); VH_DENY(mq_unlink); VH_DENY(mq_timedsend); VH_DENY(mq_timedreceive); VH_DENY(mq_notify);
    VH_DENY(mq_getsetattr);
    // Mounts and the host.
    VH_DENY(mount); VH_DENY(umount2); VH_DENY(pivot_root); VH_DENY(chroot);
    VH_DENY(open_tree); VH_DENY(move_mount); VH_DENY(fsopen); VH_DENY(fsconfig); VH_DENY(fsmount);
    VH_DENY(fspick); VH_DENY(mount_setattr);
    VH_DENY(bpf); VH_DENY(keyctl); VH_DENY(add_key); VH_DENY(request_key);
    VH_DENY(kexec_load); VH_DENY(kexec_file_load); VH_DENY(init_module); VH_DENY(finit_module);
    VH_DENY(delete_module); VH_DENY(perf_event_open); VH_DENY(userfaultfd);
    VH_DENY(io_uring_setup); VH_DENY(io_uring_enter); VH_DENY(io_uring_register);
    VH_DENY(open_by_handle_at); VH_DENY(name_to_handle_at); VH_DENY(fanotify_init);
    VH_DENY(swapon); VH_DENY(swapoff); VH_DENY(reboot); VH_DENY(syslog); VH_DENY(acct);
    // Path mutations.
    VH_DENY(unlink); VH_DENY(unlinkat); VH_DENY(rename); VH_DENY(renameat); VH_DENY(renameat2);
    VH_DENY(mkdir); VH_DENY(mkdirat); VH_DENY(rmdir); VH_DENY(link); VH_DENY(linkat);
    VH_DENY(symlink); VH_DENY(symlinkat); VH_DENY(mknod); VH_DENY(mknodat);
    VH_DENY(chmod); VH_DENY(fchmodat); VH_DENY(chown); VH_DENY(lchown); VH_DENY(fchownat);
    VH_DENY(truncate);
#undef VH_DENY

    // Opening anything for writing. memfd_create and inherited fds stay usable.
    f.deny(SCMP_SYS(creat), EACCES, "creat");
    f.deny(SCMP_SYS(openat2), ENOSYS, "openat2");
    if (denyOpenForWrite) {
        constexpr uint64_t kTmpFile = 020000000;   // __O_TMPFILE without O_DIRECTORY
        for (const uint64_t flag : {static_cast<uint64_t>(O_WRONLY), static_cast<uint64_t>(O_RDWR),
                                    static_cast<uint64_t>(O_CREAT), static_cast<uint64_t>(O_TRUNC), kTmpFile}) {
            f.denyIf(SCMP_SYS(open), EACCES, SCMP_A1(SCMP_CMP_MASKED_EQ, flag, flag), "open-write");
            f.denyIf(SCMP_SYS(openat), EACCES, SCMP_A2(SCMP_CMP_MASKED_EQ, flag, flag), "openat-write");
        }
    }

    if (!f.valid()) {
        detail += "seccomp rule setup failed: " + f.failures();
        return false;
    }
    if (const int rc = seccomp_load(f.get()); rc != 0) {
        detail += std::string("seccomp_load: ") + std::strerror(-rc) + "; ";
        return false;
    }
    return true;
}

}

Report apply(const Options& options) {
    Report report;
    report.landlockAbi = landlockAbi();
    if (options.simulateNoLandlock) report.detail += "landlock disabled (test simulation); ";
    else if (report.landlockAbi > 0) report.landlock = applyLandlock(options, report.landlockAbi, report.detail);
    else report.detail += "landlock unavailable on this kernel (CONFIG_SECURITY_LANDLOCK or lsm= boot parameter); ";

    // Render nodes are opened O_RDWR: only Landlock can scope that to the device paths. Without Landlock the
    // write-open denial stays and hardware acceleration simply fails.
    const bool denyOpenForWrite = !(options.allowGpuDevices && report.landlock);
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        report.detail += std::string("PR_SET_NO_NEW_PRIVS: ") + std::strerror(errno) + "; ";
    report.seccomp = applySeccomp(denyOpenForWrite, options.allowThreads, report.detail);
    report.openWriteDenied = report.seccomp && denyOpenForWrite;
    report.threadsDenied = report.seccomp && !options.allowThreads;
    return report;
}

}
