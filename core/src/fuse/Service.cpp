#include "fuse/Service.hpp"
#include "fuse/Bridge.hpp"
#include "runtime/Manager.hpp"
#include "concurrency/ThreadPool.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "fuse/task/Request.hpp"
#include "log/Registry.hpp"

#include <cstring>
#include <thread>
#include <atomic>
#include <utility>
#include <paths.h>
#include <sys/mount.h>    // umount2, MNT_DETACH
#include <sys/stat.h>     // lstat
#include <chrono>
#include <thread>

namespace fs = std::filesystem;
using namespace vh::concurrency;
using namespace vh::fuse;

// Best-effort guard: only allow nuking test mountpoints under /tmp
static bool isSafeTestMountpoint(const fs::path& p) {
    const auto s = p.string();
    return s.rfind("/tmp/", 0) == 0; // starts_with("/tmp/")
}

static bool isMountedOrStale(const fs::path& p) {
    // If lstat returns ENOTCONN, it’s a stale FUSE endpoint => treat as mounted.
    struct stat st{}, pst{};
    if (::lstat(p.c_str(), &st) != 0) return (errno == ENOTCONN);
    const auto parent = p.parent_path().empty() ? fs::path("/") : p.parent_path();

    // If parent can't be stat'ed, be conservative.
    if (::lstat(parent.c_str(), &pst) != 0) return true;

    // Different st_dev than parent => mountpoint boundary
    return st.st_dev != pst.st_dev;
}

static void lazyUmount(const fs::path& p) {
    // 1) Try kernel lazy detach
    if (::umount2(p.c_str(), MNT_DETACH) == 0) return;

    // 2) Fallbacks (some distros prefer fusermount3)
    (void)std::system(std::string("fusermount3 -uz " + p.string() + " >/dev/null 2>&1").c_str());
    (void)std::system(std::string("fusermount  -uz " + p.string() + " >/dev/null 2>&1").c_str());
    (void)std::system(std::string("umount     -l  " + p.string() + " >/dev/null 2>&1").c_str());
}

static void waitUnmounted(const fs::path& p, std::chrono::milliseconds timeout = std::chrono::milliseconds(1500)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!isMountedOrStale(p)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void ensureFreshMountpoint() {
    const auto mountPath = vh::paths::getMountPath();
    std::error_code ec;

    if (vh::paths::testMode) {
        if (!isSafeTestMountpoint(mountPath)) {
            throw std::runtime_error("Refusing to clear non-/tmp mount in test mode: " + mountPath.string());
        }

        if (fs::exists(mountPath, ec)) {
            // If it’s a mountpoint (or stale), detach it first.
            if (isMountedOrStale(mountPath)) {
                lazyUmount(mountPath);
                waitUnmounted(mountPath);
            }

            // Now it should be a normal dir entry; remove it entirely.
            ec.clear();
            fs::remove_all(mountPath, ec);
            if (ec && ec.value() != ENOENT) {
                vh::log::Registry::fuse()->warn(
                    "[FUSE] Failed to remove old mount dir {}: {}", mountPath.string(), ec.message());
                ec.clear();
            }
        }
    }

    // Create fresh directory (handle ENOTCONN paranoia: retry once after forced detach)
    fs::create_directories(mountPath, ec);
    if (ec) {
        if (ec.value() == ENOTCONN || ec == std::errc::not_connected) {
            // One more forceful pass if a stale endpoint just got noticed late
            lazyUmount(mountPath);
            waitUnmounted(mountPath);
            ec.clear();
            fs::remove_all(mountPath, ec); // ignore errors
            ec.clear();
            fs::create_directories(mountPath, ec);
        }
    }
    if (ec) {
        throw std::runtime_error("Failed to create mountpoint " + mountPath.string() + ": " + ec.message());
    }

    if (vh::paths::testMode) {
        fs::permissions(mountPath,
                        fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
                        fs::perm_options::replace,
                        ec);
        if (ec) {
            throw std::runtime_error("Failed to chmod mountpoint " + mountPath.string() + ": " + ec.message());
        }
    }
}

// -- FUSE class implementation --

Service::Service() : AsyncService("FUSE") {}

void fuse_ll_init(void* userdata, fuse_conn_info* conn) {
    (void)userdata;
    vh::log::Registry::fuse()->debug("[FUSE] Initializing FUSE connection...");

    constexpr uintmax_t MB = 1024 * 1024;

    conn->want |= FUSE_CAP_ASYNC_READ;
    conn->want |= FUSE_CAP_WRITEBACK_CACHE;
    conn->max_readahead = MB;
    conn->max_write = MB;

    vh::log::Registry::fuse()->debug("[FUSE] Connection initialized with max_readahead={} bytes, max_write={} bytes",
                              conn->max_readahead, conn->max_write);
}

void Service::stop() {
    if (!isRunning()) return;
    log::Registry::fuse()->info("[FUSE] Stopping FUSE connection...");
    interruptFlag_.store(true);

    if (paths::testMode) lazyUmount(paths::mountPath);

    // Only wake the loop. Do NOT unmount/destroy here.
    if (session_) fuse_session_exit(session_);

    if (worker_.joinable() && std::this_thread::get_id() != worker_.get_id())
        worker_.join();

    running_.store(false);
    interruptFlag_.store(false);
    log::Registry::fuse()->info("[FUSE] FUSE service stopped");
}

void Service::runLoop() {
    log::Registry::fuse()->debug("[FUSE] Running FUSE service");

    ensureFreshMountpoint();

    const std::vector<std::string> argsStr = {
        "vaulthalla-fuse",
        "-f",
        "-o", "allow_other",
        "-o", "auto_unmount",
        paths::getMountPath()
    };

    std::vector<std::unique_ptr<char[]>> ownedCStrs;
    std::vector<char*> argsCStr;
    for (const auto& str : argsStr) {
        auto buf = std::make_unique<char[]>(str.size() + 1);
        std::memcpy(buf.get(), str.c_str(), str.size() + 1);
        argsCStr.push_back(buf.get());
        ownedCStrs.push_back(std::move(buf));
    }

    fuse_args args = FUSE_ARGS_INIT(static_cast<int>(argsCStr.size()), argsCStr.data());

    if (fuse_opt_parse(&args, nullptr, nullptr, nullptr) == -1) {
        log::Registry::fuse()->error("[FUSE] Failed to parse FUSE options");
        return;
    }

    fuse_cmdline_opts opts{};
    if (fuse_parse_cmdline(&args, &opts) != 0) {
        log::Registry::fuse()->error("[FUSE] Failed to parse FUSE options");
        return;
    }

    fuse_lowlevel_ops ops = getOperations();
    ops.init = fuse_ll_init;

    session_ = fuse_session_new(&args, &ops, sizeof(ops), nullptr);
    if (!session_) {
        log::Registry::fuse()->error("[FUSE] Failed to create FUSE session");
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return;
    }

    if (fuse_set_signal_handlers(session_) != 0) {
        log::Registry::fuse()->error("[FUSE] Failed to set signal handlers");
        fuse_session_destroy(session_);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return;
    }

    if (fuse_session_mount(session_, opts.mountpoint) != 0) {
        log::Registry::fuse()->error("[FUSE] Failed to mount FUSE filesystem at {}", opts.mountpoint);
        fuse_remove_signal_handlers(session_);
        fuse_session_destroy(session_);
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        return;
    }

    log::Registry::fuse()->info("[FUSE] Mounted FUSE filesystem at {}", opts.mountpoint);

    while (!fuse_session_exited(session_) && !shouldStop()) {
        fuse_buf buf{};
        const int res = fuse_session_receive_buf(session_, &buf);
        if (res == -EINTR) continue;
        if (res <= 0) break;

        ThreadPoolManager::instance().fusePool()->submit(std::make_shared<task::Request>(session_, buf));
    }

    log::Registry::fuse()->info("[FUSE] FUSE service loop exiting");

    fuse_remove_signal_handlers(session_);
    fuse_session_unmount(session_);
    fuse_session_destroy(session_);
    session_ = nullptr;

    free(opts.mountpoint);
    fuse_opt_free_args(&args);

    log::Registry::fuse()->info("[FUSE] FUSE service stopped successfully");
}
