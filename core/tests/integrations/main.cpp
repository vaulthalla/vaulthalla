#include "IntegrationsTestRunner.hpp"
#include "db/Transactions.hpp"
#include "db/query/identities/User.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "config/Registry.hpp"
#include "log/Registry.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "runtime/Manager.hpp"
#include "runtime/Deps.hpp"
#include "fs/Filesystem.hpp"
#include "storage/Manager.hpp"

#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <paths.h>
#include <unistd.h>

using namespace vh::test::integration;
using namespace vh::protocols::shell;
using namespace vh::concurrency;
using namespace vh::storage;
using namespace vh::fs;

static void initBase() {
    vh::paths::enableTestMode();
    std::filesystem::remove_all(vh::paths::getBackingPath());
    std::filesystem::create_directories(vh::paths::getBackingPath());
    std::filesystem::create_directories(vh::paths::getMountPath());
    std::filesystem::create_directories(vh::paths::getRuntimePath());
    std::ofstream(vh::paths::getRuntimePath() / "superadmin_uid") << getuid() << '\n';
    vh::config::Registry::init();
    vh::log::Registry::init();
    ThreadPoolManager::instance().init();
}

static void initDB() {
    vh::db::Transactions::init();
    vh::db::seed::wipe_all_data_restart_identity();
    vh::db::seed::init_tables_if_not_exists();
    vh::db::Transactions::dbPool_->initPreparedStatements();
    vh::seed::seed_database();
}

static void initServices() {
    vh::runtime::Deps::init();
    vh::runtime::Deps::setSyncController(vh::runtime::Manager::instance().getSyncController());
    Filesystem::init(vh::runtime::Deps::get().storageManager);
    vh::runtime::Deps::get().storageManager->initStorageEngines();
    vh::runtime::Manager::instance().startTestServices();
}

static void ensureAdminExists() {
    if (!vh::db::query::identities::User::adminUserExists()) {
        vh::log::Registry::vaulthalla()->error("No admin user found; cannot run CLI tests");
        exit(1);
    }
}

static int runTests() {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    IntegrationsTestRunner runner(cli::Config::Minimal());
    const int exit_status = runner();

    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "CLI tests completed in " << static_cast<double>(duration) / 1000.0 << " seconds" << std::endl;

    return exit_status;
}

// static void shutdown() {
//     ServiceManager::instance().stopAll();
//     ThreadPoolManager::instance().shutdown();
// }

int main() {
    initBase();
    initDB();
    initServices();
    ensureAdminExists();
    const auto exit_status = runTests();
    // dont call shutdown cause it will SIGTERM the thread and exit 1
    std::_Exit(exit_status);
}
