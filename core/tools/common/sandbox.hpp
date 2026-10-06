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

#include <string>
#include <vector>

namespace vh::helpers::sandbox {

struct Options {
    bool allowGpuDevices = false;
    std::vector<std::string> extraReadOnlyPaths;
};

struct Report {
    bool landlock = false;
    int landlockAbi = 0;        // kernel ABI (0 = unavailable), even when restricting failed
    bool seccomp = false;
    bool openWriteDenied = false;   // seccomp open-for-write rule installed (false only for GPU + Landlock)
    std::string detail;             // why something is off, for the daemon's log
};

// Landlock read-only allowlist + seccomp denylist; never weakens on failure: if Landlock is unavailable it reports
// landlock=false (logged by the daemon). Sets PR_SET_NO_NEW_PRIVS. Call once, before any thread is started.
Report apply(const Options& options);

}
