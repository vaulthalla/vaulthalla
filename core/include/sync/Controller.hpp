#pragma once

#include "concurrency/AsyncService.hpp"

#include <condition_variable>
#include <memory>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

namespace vh::storage { struct Engine; }

namespace vh::sync {

struct Cloud;
struct Local;

struct FSTaskCompare {
    bool operator()(const std::shared_ptr<Local>& a, const std::shared_ptr<Local>& b) const;
};

class Controller final : public concurrency::AsyncService, std::enable_shared_from_this<Controller> {
public:
    Controller();
    ~Controller() override = default;

    void requeue(const std::shared_ptr<Local>& task);

    void interruptTask(unsigned int vaultId);

    // What an early-sync request actually did, so callers can report it truthfully.
    enum class RunNowResult {
        Started,      // a fresh sync task was scheduled to run immediately
        Rerun,        // a sync was already running; an immediate rerun was queued behind it
        NoTask        // no sync task exists for the vault (unknown vault, or its engine isn't loaded)
    };

    RunNowResult runNow(unsigned int vaultId, uint8_t trigger = 3); // Event::Trigger::WEBHOOK

    void refreshEngines();

protected:
    void runLoop() override;
    void onStop() override;

private:
    friend struct Local;
    friend struct Cloud;

    std::priority_queue<std::shared_ptr<Local>,
                    std::vector<std::shared_ptr<Local>>,
                    FSTaskCompare> pq;

    mutable std::mutex pqMutex_;
    // Signalled whenever a task is queued and on stop, so runLoop can sleep until the earliest task is due.
    std::condition_variable pqCv_;
    mutable std::shared_mutex taskMapMutex_;

    std::unordered_map<unsigned int, std::shared_ptr<Local>> taskMap_{};

    void pruneStaleTasks(const std::vector<std::shared_ptr<storage::Engine>>& engines);

    void processTask(const std::shared_ptr<storage::Engine>& engine);

    std::shared_ptr<Local> createTask(const std::shared_ptr<storage::Engine>& engine);

    template <typename T>
    std::shared_ptr<T> createTask(const std::shared_ptr<storage::Engine>& engine) {
        auto task = std::make_shared<T>(engine);
        return task;
    }
};

}
