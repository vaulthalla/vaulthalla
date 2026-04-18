// Services
#include "../include/protocols/ProtocolService.hpp"
#include "runtime/Manager.hpp"
#include "runtime/Deps.hpp"

// Database
#include "db/Transactions.hpp"
#include "db/query/identities/User.hpp"

// Storage
#include "storage/Manager.hpp"
#include "fs/Filesystem.hpp"

// Seed
#include "seed/include/seed_db.hpp"
#include "seed/include/init_db_tables.hpp"

// Misc
#include "config/Registry.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "log/Registry.hpp"

// Libraries
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
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
    ThreadPoolManager::instance().shutdown();

    log->info("[✓] Vaulthalla services shut down cleanly.");
}
}

int main() {
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
        vh::log::Registry::vaulthalla()->error(
            "[-] Failed to initialize Vaulthalla: {}", e.what()
        );
        return EXIT_FAILURE;
    }
}
