#pragma once

#include "storage/CloudEngine.hpp"
#include "storage/s3/Controller.hpp"

namespace vh::storage {

class ScopedS3RequestBudget final {
public:
    ScopedS3RequestBudget(const CloudEngine& engine, const s3::S3RequestBudget& budget)
        : engine_(&engine) {
        engine_->resetS3RequestMetrics();
        engine_->configureS3RequestBudget(budget);
    }

    ScopedS3RequestBudget(const std::shared_ptr<CloudEngine>& engine, const s3::S3RequestBudget& budget)
        : ScopedS3RequestBudget(*engine, budget) {}

    ScopedS3RequestBudget(const ScopedS3RequestBudget&) = delete;
    ScopedS3RequestBudget& operator=(const ScopedS3RequestBudget&) = delete;

    ~ScopedS3RequestBudget() {
        if (!engine_) return;
        try {
            engine_->clearS3RequestBudget();
        } catch (...) {
        }
    }

    [[nodiscard]] s3::S3RequestMetrics metrics() const {
        return engine_ ? engine_->s3RequestMetrics() : s3::S3RequestMetrics{};
    }

private:
    const CloudEngine* engine_{};
};

}
