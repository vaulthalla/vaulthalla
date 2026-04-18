#pragma once

#include "concurrency/AsyncService.hpp"

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

    void runNow(unsigned int vaultId, uint8_t trigger = 3); // Event::Trigger::WEBHOOK

protected:
    void runLoop() override;

private:
    friend struct Local;
    friend struct Cloud;

    std::priority_queue<std::shared_ptr<Local>,
                    std::vector<std::shared_ptr<Local>>,
                    FSTaskCompare> pq;

    mutable std::mutex pqMutex_;
    mutable std::shared_mutex taskMapMutex_;

    std::unordered_map<unsigned int, std::shared_ptr<Local>> taskMap_{};

    void refreshEngines();

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
