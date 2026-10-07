// Services
#include "../include/protocols/ProtocolService.hpp"
#include "runtime/Manager.hpp"
#include "runtime/Deps.hpp"

// Database
#include "db/Transactions.hpp"
#include "auth/Bootstrap.hpp"
#include "db/query/identities/User.hpp"

// Storage
#include "storage/Manager.hpp"
#include "fs/Filesystem.hpp"

// Previews
#include "preview/cache/Maintenance.hpp"
#include "preview/derive/Queue.hpp"

// Seed
#include "seed/include/seed_db.hpp"
#include "seed/include/init_db_tables.hpp"

// Misc
#include "config/Registry.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "concurrency/ThreadPoolManager.hpp"
#include "log/Registry.hpp"

// Libraries
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <execinfo.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <pdfium/fpdfview.h>

using namespace vh::config;
using namespace vh::concurrency;
using namespace vh::storage;
using namespace vh::fs;

namespace {
std::atomic shouldExit = false;

void signalHandler(const int signum) {
    vh::log::Registry::vaulthalla()->info(
        "[!] Signal {} received. Shutting down gracefully...",
        std::to_string(signum)
    );
    shouldExit = true;
}

// A crashing FUSE daemon must die at once. A core dump first waits for every thread to stop, but a thread in close()
// on a file of our own mount (HTTP upload staging goes through FUSE) waits for a FUSE reply this process can no
// longer send: the dump never finishes, the mount hangs and so does everything touching it, apt included, while
// Restart=on-failure never fires. Not being dumpable skips the dump (and keeps decrypted bytes out of cores, as
// LimitCORE=0 intends), so the kernel kills the process, the mount aborts and systemd restarts the daemon. The
// handler leaves a backtrace in the journal in place of the core.
void fatalSignalHandler(const int signum) {
    static constexpr char kHeader[] = "[vaulthalla] fatal signal, backtrace:\n";
    (void)!::write(STDERR_FILENO, kHeader, sizeof(kHeader) - 1);
    void* frames[64];
    const int depth = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, depth, STDERR_FILENO);
    // SA_RESETHAND restored the default action: re-raise to terminate with the original signal.
    ::raise(signum);
}

void installCrashGuard() {
    if (::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0)
        std::fprintf(stderr, "[vaulthalla] could not disable core dumps: %s\n", std::strerror(errno));

    // backtrace() loads libgcc on first use, which allocates: do it now, never from a handler on a broken heap.
    void* warm[1];
    (void)::backtrace(warm, 1);

    struct sigaction action{};
    action.sa_handler = fatalSignalHandler;
    action.sa_flags = SA_RESETHAND | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
        ::sigaction(sig, &action, nullptr);
}

void registerSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

// --- External Libs ---

struct PdfiumGuard {
    PdfiumGuard() {
        FPDF_LIBRARY_CONFIG config;
        config.version = 3;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
    }

    ~PdfiumGuard() {
        FPDF_DestroyLibrary();
    }
};

// --- Core Init ---

void initDB() {
    vh::db::Transactions::init();
    vh::db::seed::init_tables_if_not_exists();
    vh::db::Transactions::dbPool_->initPreparedStatements();

    if (!vh::db::query::identities::User::adminUserExists())
        vh::seed::seed_database();

    vh::seed::reconcileSystemPrincipals();
    vh::auth::bootstrap::retireLegacyDefaultPassword();
    vh::seed::reconcileGlobalVaultPolicies();
}

void initDeps() {
    vh::runtime::Deps::init();
    vh::runtime::Deps::setSyncController(
        vh::runtime::Manager::instance().getSyncController()
    );
}

// --- Wiring ---

void wireStorage() {
    Filesystem::init(vh::runtime::Deps::get().storageManager);
    vh::runtime::Deps::get().storageManager->initStorageEngines();

    // Every start (so every upgraded install): legacy plaintext thumbnails and orphaned artifacts go away before
    // anything can serve them.
    vh::preview::cache::sweepAtStartup(vh::runtime::Deps::get().storageManager->getEngines());
}

// --- Runtime ---

void startRuntime() {
    vh::runtime::Manager::instance().startAll();
}

void stopRuntime() {
    vh::runtime::Manager::instance().stopAll(SIGTERM);
}

// --- Orchestration ---

void startVaulthalla() {
    const auto log = vh::log::Registry::vaulthalla();

    ThreadPoolManager::instance().init();

    // Reader policies (preview.media.*) and the derived-artifact negative-cache TTL.
    vh::preview::cache::applyConfig();

    log->info("[*] Initializing database...");
    initDB();

    log->info("[*] Initializing runtime dependencies...");
    initDeps();

    log->info("[*] Wiring storage layer...");
    wireStorage();

    log->info("[*] Starting runtime...");
    startRuntime();

    log->info("[✓] Started Vaulthalla - The Final Cloud.");
}

void shutdownVaulthalla() {
    auto log = vh::log::Registry::vaulthalla();

    log->info("[*] Shutting down Vaulthalla services...");

    stopRuntime();
    // No request can enqueue any more: SIGKILL running converter helpers and join the derive workers while the
    // storage engines and the DB pool they use are still up.
    vh::preview::derive::Queue::instance().shutdown();
    ThreadPoolManager::instance().shutdown();

    log->info("[✓] Vaulthalla services shut down cleanly.");
}
}

int main() {
    installCrashGuard();

    try {
        Registry::init();
        vh::log::Registry::init();

        PdfiumGuard pdfium;

        startVaulthalla();
        registerSignalHandlers();

        while (!shouldExit)
            std::this_thread::sleep_for(std::chrono::seconds(1));

        shutdownVaulthalla();
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        // Config is loaded before logging, so a malformed or missing config.yaml lands here with no logger;
        // calling it would throw again and abort with a core dump instead of this message (vh-storage).
        try {
            vh::log::Registry::vaulthalla()->error("[-] Failed to initialize Vaulthalla: {}", e.what());
        } catch (...) {
            std::fprintf(stderr, "[-] Failed to initialize Vaulthalla: %s\n", e.what());
        }
        return EXIT_FAILURE;
    }
}
