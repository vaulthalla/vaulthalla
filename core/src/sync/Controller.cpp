#include "sync/Controller.hpp"
#include "storage/Manager.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "sync/Local.hpp"
#include "sync/Cloud.hpp"
#include "concurrency/ThreadPool.hpp"
#include "storage/CloudEngine.hpp"
#include "db/query/vault/Vault.hpp"
#include "vault/model/Vault.hpp"
#include "runtime/Deps.hpp"
#include "log/Registry.hpp"

#include <boost/dynamic_bitset.hpp>
#include <thread>

using namespace vh::storage;
using namespace vh::concurrency;
using namespace vh::vault::model;
using namespace vh::sync;

bool FSTaskCompare::operator()(const std::shared_ptr<Local>& a, const std::shared_ptr<Local>& b) const {
    return a->next_run > b->next_run; // Min-heap based on next_run time
}

Controller::Controller()
    : AsyncService("SyncController") {}

void Controller::requeue(const std::shared_ptr<Local>& task) {
    std::scoped_lock lock(pqMutex_);
    pq.push(task);
    log::Registry::sync()->debug("[SyncController] Requeued task for vault ID: {}", task->vaultId());
}

void Controller::interruptTask(const unsigned int vaultId) {
    std::scoped_lock lock(taskMapMutex_, pqMutex_);

    if (!taskMap_.contains(vaultId)) {
        log::Registry::sync()->error("[SyncController] No task found for vault ID: {}", vaultId);
        return;
    }

    if (const auto& task = taskMap_[vaultId]) {
        task->interrupt();
        log::Registry::sync()->info("[SyncController] Interrupted task for vault ID: {}", vaultId);
    } else log::Registry::sync()->error("[SyncController] Task for vault ID: {} is null", vaultId);
}

void Controller::runLoop() {
    refreshEngines();
    std::chrono::system_clock::time_point lastRefresh = std::chrono::system_clock::now();
    unsigned int refreshTries = 0;

    while (!shouldStop()) {
        if (std::chrono::system_clock::now() - lastRefresh > std::chrono::minutes(5)) {
            log::Registry::sync()->debug("[SyncController] Refreshing sync engines...");
            refreshEngines();
            lastRefresh = std::chrono::system_clock::now();
        }

        bool pqIsEmpty = false;
        {
            std::scoped_lock lock(pqMutex_);
            pqIsEmpty = pq.empty();
        }

        if (pqIsEmpty) {
            if (++refreshTries > 3) lazySleep(std::chrono::seconds(3));
            refreshEngines();
            continue;
        }

        refreshTries = 0;
        std::shared_ptr<Local> task;

        {
            std::scoped_lock lock(pqMutex_);
            task = pq.top();
            pq.pop();
        }

        if (!task || task->isInterrupted()) continue;

        if (task->next_run <= std::chrono::system_clock::now()) ThreadPoolManager::instance().syncPool()->submit(task);
        else {
            std::scoped_lock lock(pqMutex_);
            pq.push(task);
        }
    }
}

void Controller::runNow(const unsigned int vaultId, const uint8_t trigger) {
    log::Registry::sync()->debug("[SyncController] Early sync request for vault ID: {}", vaultId);

    std::shared_ptr<Local> task;

    {
        std::scoped_lock lock(taskMapMutex_);
        if (!taskMap_.contains(vaultId)) {
            log::Registry::sync()->error("[SyncController] No task found for vault ID: {}", vaultId);
            return;
        }
        task = taskMap_[vaultId];
    }

    task->interrupt();

    while (task->isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    task = createTask(task->engine);
    task->runNow(trigger);

    {
        std::scoped_lock lock(taskMapMutex_, pqMutex_);
        taskMap_[vaultId] = task;
        pq.push(task);
    }
}

void Controller::refreshEngines() {
    const auto latestEngines = runtime::Deps::get().storageManager->getEngines();
    pruneStaleTasks(latestEngines);
    for (const auto& engine : latestEngines) processTask(engine);
}

void Controller::pruneStaleTasks(const std::vector<std::shared_ptr<Engine> >& engines) {
    boost::dynamic_bitset<> latestBitset(db::query::vault::Vault::maxVaultId() + 1);
    for (const auto& engine : engines) latestBitset.set(engine->vault->id);

    {
        std::unique_lock lock(taskMapMutex_);

        // Remove engines that are no longer present
        std::vector<unsigned int> staleIds;

        for (const auto& [id, task] : taskMap_)
            if (!latestBitset[task->vaultId()]) staleIds.push_back(id);

        for (auto id : staleIds) taskMap_.erase(id);
    }
}


void Controller::processTask(const std::shared_ptr<Engine>& engine) {
    std::scoped_lock lock(taskMapMutex_, pqMutex_);

    if (!taskMap_.contains(engine->vault->id)) {
        const auto task = createTask(engine);
        taskMap_[engine->vault->id] = task;
        pq.push(task);
    }
}

std::shared_ptr<Local> Controller::createTask(const std::shared_ptr<Engine>& engine) {
    if (engine->type() == StorageType::Local) return createTask<Local>(engine);
    if (engine->type() == StorageType::Cloud) return createTask<Cloud>(engine);
    throw std::runtime_error("Unsupported StorageType: " + std::to_string(static_cast<int>(engine->type())));
}
