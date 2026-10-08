#pragma once

// Self-applied confinement for preview converter helpers (never linked into the daemon).
//
// Landlock: read-only allowlist (/usr, /lib*, /etc/ld.so.*, /proc/self, /sys/devices/system/cpu, /dev/null,
// /dev/urandom, plus extraReadOnlyPaths); /dev/dri and /dev/nvidia* read-write (+ /sys/dev/char, /sys/devices
// read-only) only with allowGpuDevices. On a
// Landlock ABI >= 4 kernel TCP bind/connect are denied too, on ABI >= 6 abstract unix sockets and signals to
// processes outside the sandbox.
//
// seccomp (libseccomp denylist, default allow): sockets (socket/socketpair/connect/bind/listen/accept*, sendto with
// an address), execve/execveat, fork/vfork and clone without CLONE_THREAD or with namespace flags (clone3 -> ENOSYS
// so libc falls back to clone), ptrace, process_vm_readv/writev, signals to other processes, pidfd_*,
// mount family/unshare/setns/pivot_root/chroot, bpf, keyctl/add_key/request_key, kexec_*, *_module,
// perf_event_open, io_uring_*, userfaultfd, path mutations (unlink/rename/mkdir/link/symlink/mknod/chmod/chown/
// truncate), and open/openat/creat with write/create/truncate flags (EACCES; openat2 -> ENOSYS because its flags
// cannot be inspected). With allowGpuDevices AND an active Landlock the open-for-write rule is left to Landlock
// (render nodes are opened O_RDWR); without Landlock it stays, so GPU access fails instead of the sandbox weakening.
//
// The daemon runs as the same uid (and is the helper's parent), so everything that acts on another process by pid
// is limited to the helper itself: prlimit64 only with pid 0; setpriority only (PRIO_PROCESS, 0); ioprio_set only
// (IOPRIO_WHO_PROCESS, 0); sched_setaffinity/setscheduler/setparam/setattr and migrate_pages/move_pages only with
// pid 0; process_madvise/process_mrelease denied. No fd can be made to signal another process: fcntl F_SETOWN,
// F_SETOWN_EX, F_SETSIG and F_SETLEASE and ioctl FIOSETOWN/SIOCSPGRP are denied. SysV IPC (shm*/msg*/sem*) and
// POSIX message queues (mq_*) are denied, so nothing the helper creates outlives it. Reading another process's
// /proc/<pid> (and ptrace-class access such as /proc/<pid>/mem) is Landlock's job: only /proc/self is allowed and
// Landlock denies ptrace access to processes outside the domain.
//
// Threads: with allowThreads=false (the CAD helper, which is single-threaded) clone is denied outright. Helpers that
// need threads (FFmpeg) are bounded by the daemon instead (Limits::maxThreads, see preview/derive/Sandbox.hpp).
//
// A helper without Landlock or without seccomp refuses to run (protocol.hpp, runMain): seccomp alone cannot stop a
// same-uid process from reading the daemon's files or /proc/<daemon>/mem.

#include <string>
#include <vector>

namespace vh::helpers::sandbox {

struct Options {
    bool allowGpuDevices = false;
    bool allowThreads = true;          // false: clone is denied outright (single-threaded helpers)
    bool simulateNoLandlock = false;   // tests: behave as on a kernel without Landlock (only ever weakens the
                                       // outcome to a refusal; seccomp is still installed)
    std::vector<std::string> extraReadOnlyPaths;
};

struct Report {
    bool landlock = false;
    int landlockAbi = 0;        // kernel ABI (0 = unavailable), even when restricting failed
    bool seccomp = false;
    bool openWriteDenied = false;   // seccomp open-for-write rule installed (false only for GPU + Landlock)
    bool threadsDenied = false;     // clone denied outright (allowThreads=false)
    std::string detail;             // why something is off, for the daemon's log
};

// Landlock read-only allowlist + seccomp denylist; never weakens on failure: if Landlock is unavailable it reports
// landlock=false and the caller refuses to run. Sets PR_SET_NO_NEW_PRIVS. Call once, before any thread is started
// and before any device or driver initialisation (landlock_restrict_self only confines the calling thread).
Report apply(const Options& options);

}
