#pragma once

#include <cstdint>
#include <ctime>

namespace vh::sync::model {

struct ScopedOp {
    uint64_t size_bytes{};
    std::time_t timestamp_begin{};
    std::time_t timestamp_end{};
    bool success{};

    void start();
    void start(uint64_t size_bytes);
    void stop();
    [[nodiscard]] uint64_t duration_ms() const;
};

}
