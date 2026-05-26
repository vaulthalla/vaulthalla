#include "sync/Local.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "concurrency/ThreadPool.hpp"
#include "sync/Controller.hpp"
#include "storage/Engine.hpp"
#include "storage/CloudEngine.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/Operation.hpp"
#include "vault/model/Vault.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "fs/ops/file.hpp"
#include "db/query/sync/Operation.hpp"
#include "db/query/fs/File.hpp"
#include "vault/EncryptionManager.hpp"
#include "fs/Filesystem.hpp"
#include "runtime/Deps.hpp"
#include "log/Registry.hpp"
#include "concurrency/taskOpRanges.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/Throughput.hpp"
#include "sync/model/ScopedOp.hpp"
#include "db/query/sync/Policy.hpp"
#include "sync/tasks/RotateKey.hpp"
#include "sync/tasks/Delete.hpp"
#include "storage/s3/Controller.hpp"

using namespace vh::sync;
using namespace vh::sync::model;
using namespace vh::storage;
using namespace vh::services;
using namespace vh::vault::model;
using namespace vh::concurrency;
using namespace std::chrono;
using namespace vh::fs;
using namespace vh::fs::model;
using namespace vh::fs::ops;

Local::Local(const std::shared_ptr<Engine>& engine)
    : engine(engine),
      event(std::make_shared<Event>()) {
    if (!engine || !engine->sync) {
        next_run = system_clock::now() + Policy::DEFAULT_SYNC_INTERVAL;
        return;
    }

    engine->sync->interval = Policy::clampInterval(engine->sync->interval);
    next_run = system_clock::from_time_t(engine->sync->last_sync_at) + seconds(engine->sync->interval.count());
}

void Local::operator()() {
    startTask();

    const Stage stages[] = {
        {"shared",   [this]{ processSharedOps(); }}
    };

    runStages(stages);
    shutdown();
}

void Local::handleInterrupt() const { if (isInterrupted()) throw std::runtime_error("Sync task interrupted"); }

bool Local::isRunning() const { return runningFlag.load(); }

void Local::interrupt() { interruptFlag.store(true); }

bool Local::isInterrupted() const { return interruptFlag.load(); }

void Local::runStages(const std::span<const Stage> stages) const {
    for (const auto& [name, fn] : stages) {
        if (!isRunning()) break;
        try {
            fn();
            if (markBudgetExceededIfAny()) break;
            handleInterrupt();
            if (event) event->heartbeat();
        } catch (const vh::storage::s3::RequestBudgetExceeded& e) {
            event->status = Event::Status::STALLED;
            event->stall_reason = e.what();
            break;
        } catch (const SyncStalled& e) {
            event->status = Event::Status::STALLED;
            event->stall_reason = e.what();
            break;
        } catch (const std::exception& e) {
            handleError(std::format("[FSTask:{}] {}", std::string(name), e.what()));
            break;
        } catch (...) {
            handleError(std::format("[FSTask:{}] Unknown exception", std::string(name)));
            break;
        }
    }
}

void Local::startTask() {
    if (!engine) {
        log::Registry::sync()->error("[FSTask] Engine is null, cannot proceed with sync.");
        return;
    }

    log::Registry::sync()->debug("[FSTask] Starting sync for vault '{}'", engine->vault->id);

    interruptFlag.store(false);
    runningFlag.store(true);
    db::query::sync::Policy::reportSyncStarted(engine->sync->id);

    newEvent();
    event = engine->latestSyncEvent;
    if (!event) {
        runningFlag.store(false);
        throw std::runtime_error("[FSTask] Failed to create sync event");
    }
    event->status = Event::Status::RUNNING;
    engine->saveSyncEvent();
    event->start();
}

void Local::processSharedOps() {
    struct NamedOp { const char* name; std::function<void()> fn; };

    const std::vector<NamedOp> ops = {
        {"processOperations", [this]{ processOperations(); }},
        {"removeTrashedFiles", [this]{ removeTrashedFiles(); }},
        {"handleVaultKeyRotation", [this]{ handleVaultKeyRotation(); }},
      };

    for (const auto& [name, op] : ops) {
        if (!isRunning()) break;

        try {
            op();
            if (markBudgetExceededIfAny()) break;
            handleInterrupt();
            event->heartbeat();
        } catch (const vh::storage::s3::RequestBudgetExceeded& e) {
            event->status = Event::Status::STALLED;
            event->stall_reason = e.what();
            break;
        } catch (const SyncStalled& e) {
            event->status = Event::Status::STALLED;
            event->stall_reason = e.what();
            break;
        } catch (const std::exception& e) {
            handleError(std::format("[FSTask] Exception during {}: {}", name, e.what()));
            break;
        }
    }
}

bool Local::markBudgetExceededIfAny() const {
    if (!engine || !event || engine->type() != StorageType::Cloud) return false;

    const auto cloud = std::static_pointer_cast<CloudEngine>(engine);
    const auto metrics = cloud->s3RequestMetrics();
    event->applyS3RequestMetrics(metrics);
    if (!metrics.budget_exceeded) return false;

    event->status = Event::Status::STALLED;
    event->stall_reason = metrics.budget_exceeded_reason;
    event->error_code.clear();
    event->error_message.clear();
    return true;
}

void Local::handleError(const std::string& message) const {
    log::Registry::sync()->error("[FSTask] {}", message);
    event->error_message = message;
    event->status = Event::Status::ERROR;
}

void Local::shutdown() {
    runningFlag.store(false);
    futures.clear();
    event->stop();
    event->parseCurrentStatus();
    engine->saveSyncEvent();
    if (event->status == Event::Status::SUCCESS) {
        db::query::sync::Policy::reportSyncSuccess(engine->sync->id);
        uint8_t pending{};
        if (consumeRunAfterCurrent(pending)) {
            if (runtime::Deps::get().syncController) {
                runtime::Deps::get().syncController->runNow(engine->vault->id, pending);
            } else {
                runNow(pending);
                requeue();
            }
            log::Registry::sync()->debug(
                "[FSTask] Sync task queued for immediate rerun for vault '{}'",
                engine->vault->id);
            log::Registry::sync()->info("[FSTask] Sync completed for vault '{}' in {}s",
                                  engine->vault->id, event->durationSeconds());
            return;
        }

        next_run = system_clock::now() + seconds(engine->sync->interval.count());
        requeue();
        log::Registry::sync()->debug("[FSTask] Sync task requeued for vault '{}'", engine->vault->id);
        log::Registry::sync()->info("[FSTask] Sync completed for vault '{}' in {}s",
                              engine->vault->id, event->durationSeconds());
    } else {
        pendingRunNowFlag.store(false);
        log::Registry::sync()->error("[FSTask] Sync failed for vault '{}': {}", engine->vault->id, event->error_message);
    }
}

void Local::newEvent() {
    if (!runNowFlag) engine->newSyncEvent();
    else {
        engine->newSyncEvent(trigger);
        runNowFlag = false;
    }
}

void Local::processFutures() {
    for (auto& f : futures)
        if (std::get<bool>(f.get()) == false)
            log::Registry::sync()->error("[FSTask] Future failed");
    futures.clear();
}

unsigned int Local::vaultId() const { return engine->vault->id; }

void Local::requeue() {
    next_run = system_clock::now() + seconds(engine->sync->interval.count());
    runtime::Deps::get().syncController->requeue(shared_from_this());
}

void Local::runNow(const uint8_t trigger) {
    runNowFlag = true;
    this->trigger = trigger;
    next_run = system_clock::now();
}

void Local::requestRunAfterCurrent(const uint8_t trigger) {
    pendingTrigger.store(trigger);
    pendingRunNowFlag.store(true);
}

bool Local::consumeRunAfterCurrent(uint8_t& trigger) {
    if (!pendingRunNowFlag.exchange(false)) return false;
    trigger = pendingTrigger.load();
    return true;
}

void Local::push(const std::shared_ptr<Task>& task) {
    futures.push_back(task->getFuture().value());
    concurrency::ThreadPoolManager::instance().syncPool()->submit(task);
}

std::shared_ptr<ScopedOp> Local::op(const Throughput::Metric& metric) const {
    return event->getOrCreateThroughput(metric).newOp();
}

void Local::processOperations() const {
    for (const auto& op : db::query::sync::Operation::listOperationsByVault(engine->vault->id)) {
        const auto scopedOp = event->getOrCreateThroughput(op->opToThroughputMetric()).newOp();
        scopedOp->start();

        const auto absSrc = engine->paths->absPath(op->source_path, PathType::BACKING_VAULT_ROOT);
        const auto absDest = engine->paths->absPath(op->destination_path, PathType::BACKING_VAULT_ROOT);
        if (absDest.has_parent_path())
            if (const auto err = Filesystem::mkdir({.path = absDest.parent_path()}); err)
                handleError(std::format("[FSTask] Failed to create parent directory for '{}': {}", absDest.parent_path().string(), std::strerror(err)));

        const auto f = db::query::fs::File::getFileByPath(engine->vault->id, op->destination_path);
        if (!f) {
            log::Registry::sync()->error("[FSTask] File not found for operation: {}", op->destination_path);
            scopedOp->stop();
            continue;
        }

        scopedOp->size_bytes = f->size_bytes;

        if (f->size_bytes == 0 && op->operation != Operation::Op::Copy) {
            log::Registry::sync()->error("[FSTask] File size is zero for operation: {}", op->destination_path);
            scopedOp->stop();
            continue;
        }

        const auto tmpPath = decrypt_file_to_temp(vaultId(), op->source_path, engine);
        const auto buffer = readFileToVector(tmpPath);

        if (buffer.empty()) {
            log::Registry::sync()->error("[FSTask] Empty file buffer for operation: {}", op->source_path);
            scopedOp->stop();
            continue;
        }

        const auto ciphertext = engine->encryptionManager->encrypt(buffer, f);
        writeFile(absDest, ciphertext);
        db::query::fs::File::setEncryptionIVAndVersion(f);

        const auto& move = [&]() {
            if (std::filesystem::exists(absSrc)) std::filesystem::remove(absSrc);
            engine->moveThumbnails(op->source_path, op->destination_path);
        };

        if (op->operation == Operation::Op::Copy) engine->copyThumbnails(op->source_path, op->destination_path);
        else if (op->operation == Operation::Op::Move || op->operation == Operation::Op::Rename) move();
        else throw std::runtime_error("Unknown operation type: " + std::to_string(static_cast<int>(op->operation)));

        scopedOp->stop();
    }
}

void Local::handleVaultKeyRotation() {
    try {
        if (!engine->encryptionManager->rotation_in_progress()) return;

        const auto filesToRotate = db::query::fs::File::getFilesOlderThanKeyVersion(engine->vault->id, engine->encryptionManager->get_key_version());
        if (filesToRotate.empty()) {
            log::Registry::audit()->info("[FSTask] No files to rotate for vault '{}'", engine->vault->id);
            engine->encryptionManager->finish_key_rotation();
            return;
        }

        for (const auto& [begin, end] : getTaskOperationRanges(filesToRotate.size()))
            push(std::make_shared<tasks::RotateKey>(engine, filesToRotate, begin, end));

        processFutures();

        engine->encryptionManager->finish_key_rotation();

        log::Registry::audit()->info("[FSTask] Vault key rotation finished for vault '{}'", engine->vault->id);
    } catch (const std::exception& e) {
        log::Registry::sync()->error("[FSTask] Exception during vault key rotation for vault '{}': {}", engine->vault->id, e.what());
        runningFlag.store(false);
    } catch (...) {
        log::Registry::sync()->error("[FSTask] Unknown exception during vault key rotation for vault '{}'", engine->vault->id);
        runningFlag.store(false);
    }
}

void Local::removeTrashedFiles() {
    const auto files = db::query::fs::File::listTrashedFiles(engine->vault->id);
    const auto type = engine->type() == StorageType::Local ? tasks::Delete::Type::LOCAL : tasks::Delete::Type::PURGE;

    futures.reserve(files.size());
    for (const auto& file : files)
        push(std::make_shared<tasks::Delete>(engine, file, op(Throughput::Metric::DELETE), type));

    processFutures();
}
