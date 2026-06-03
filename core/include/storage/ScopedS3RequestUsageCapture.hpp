#pragma once

#include "storage/CloudEngine.hpp"
#include "storage/s3/Controller.hpp"
#include "storage/s3/RequestUsage.hpp"

#include <mutex>
#include <optional>

namespace vh::storage {

class ScopedS3RequestUsageCapture final {
public:
    explicit ScopedS3RequestUsageCapture(
        const CloudEngine& engine,
        std::optional<s3::S3RequestBudget> budget = std::nullopt);
    explicit ScopedS3RequestUsageCapture(
        const std::shared_ptr<CloudEngine>& engine,
        std::optional<s3::S3RequestBudget> budget = std::nullopt);

    ScopedS3RequestUsageCapture(const ScopedS3RequestUsageCapture&) = delete;
    ScopedS3RequestUsageCapture& operator=(const ScopedS3RequestUsageCapture&) = delete;

    ~ScopedS3RequestUsageCapture();

    [[nodiscard]] s3::S3GatewayUpstreamUsage usage() const;

private:
    friend class s3::Controller;

    void recordList(uint64_t amount);
    void recordHead(uint64_t amount);
    void recordGet(uint64_t amount);
    void recordPut(uint64_t amount);
    void recordCopy(uint64_t amount);
    void recordDelete(uint64_t amount);
    void recordDownloadBytes(uint64_t amount);
    void recordUploadBytes(uint64_t amount);

    void checkList(uint64_t amount) const;
    void checkHead(uint64_t amount) const;
    void checkGet(uint64_t amount) const;
    void checkPut(uint64_t amount) const;
    void checkCopy(uint64_t amount) const;
    void checkDelete(uint64_t amount) const;
    void checkDownloadBytes(uint64_t amount) const;

    void checkCount(uint64_t current, const std::optional<uint64_t>& limit, uint64_t amount, const char* label) const;
    void markTouchedLocked();

    const CloudEngine* engine_{};
    std::optional<s3::S3RequestBudget> budget_;
    mutable std::mutex mutex_;
    s3::S3GatewayUpstreamUsage usage_;
};

} // namespace vh::storage
