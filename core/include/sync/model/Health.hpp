#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace vh::sync::model {

struct Health {
    std::string sync_state;
    uint64_t pending_sync_tasks{}, failed_sync_tasks_24hr{}, failed_sync_tasks_7d{}, retry_count{};
    uintmax_t avg_throughput_bytes{}, peak_throughput_bytes{};
    std::time_t last_sync, oldest_pending_task_age;
    bool isDivergence{};
};

}
