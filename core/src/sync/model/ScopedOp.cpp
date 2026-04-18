#include "sync/model/ScopedOp.hpp"

#include <chrono>

using namespace vh::sync::model;
using namespace std::chrono;

void ScopedOp::start() { timestamp_begin = system_clock::to_time_t(system_clock::now()); }
void ScopedOp::stop() { timestamp_end = system_clock::to_time_t(system_clock::now()); }

void ScopedOp::start(const uint64_t size_bytes) {
    this->size_bytes = size_bytes;
    start();
}

uint64_t ScopedOp::duration_ms() const {
    return duration_cast<milliseconds>(system_clock::from_time_t(timestamp_end) - system_clock::from_time_t(timestamp_begin)).count();
}
