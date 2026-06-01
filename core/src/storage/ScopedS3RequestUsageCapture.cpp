#include "storage/ScopedS3RequestUsageCapture.hpp"

namespace vh::storage {

ScopedS3RequestUsageCapture::ScopedS3RequestUsageCapture(
    const CloudEngine& engine,
    std::optional<s3::S3RequestBudget> budget)
    : engine_(&engine),
      budget_(std::move(budget)) {
    s3::Controller::pushRequestUsageCapture(this);
}

ScopedS3RequestUsageCapture::ScopedS3RequestUsageCapture(
    const std::shared_ptr<CloudEngine>& engine,
    std::optional<s3::S3RequestBudget> budget)
    : ScopedS3RequestUsageCapture(*engine, std::move(budget)) {}

ScopedS3RequestUsageCapture::~ScopedS3RequestUsageCapture() {
    s3::Controller::popRequestUsageCapture(this);
}

s3::S3GatewayUpstreamUsage ScopedS3RequestUsageCapture::usage() const {
    std::scoped_lock lock(mutex_);
    return usage_;
}

void ScopedS3RequestUsageCapture::checkCount(
    const uint64_t current,
    const std::optional<uint64_t>& limit,
    const uint64_t amount,
    const char* label) const {
    if (limit && current + amount > *limit)
        throw s3::RequestBudgetExceeded(std::string{"S3 request budget exceeded for "} + label, label);
}

void ScopedS3RequestUsageCapture::checkList(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.list_requests, budget_->max_list_requests, amount, "LIST");
}

void ScopedS3RequestUsageCapture::checkHead(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.head_requests, budget_->max_head_requests, amount, "HEAD");
}

void ScopedS3RequestUsageCapture::checkGet(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.get_requests, budget_->max_get_requests, amount, "GET");
}

void ScopedS3RequestUsageCapture::checkPut(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.put_requests, budget_->max_put_requests, amount, "PUT");
}

void ScopedS3RequestUsageCapture::checkCopy(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.copy_requests, budget_->max_copy_requests, amount, "COPY");
}

void ScopedS3RequestUsageCapture::checkDelete(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.delete_requests, budget_->max_delete_requests, amount, "DELETE");
}

void ScopedS3RequestUsageCapture::checkDownloadBytes(const uint64_t amount) const {
    if (!budget_) return;
    std::scoped_lock lock(mutex_);
    checkCount(usage_.downloaded_bytes, budget_->max_downloaded_bytes, amount, "downloaded bytes");
}

void ScopedS3RequestUsageCapture::markTouchedLocked() {
    usage_.touched_upstream = !usage_.empty();
}

void ScopedS3RequestUsageCapture::recordList(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.list_requests += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordHead(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.head_requests += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordGet(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.get_requests += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordPut(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.put_requests += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordCopy(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.copy_requests += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordDelete(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.delete_requests += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordDownloadBytes(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.downloaded_bytes += amount;
    markTouchedLocked();
}

void ScopedS3RequestUsageCapture::recordUploadBytes(const uint64_t amount) {
    std::scoped_lock lock(mutex_);
    usage_.uploaded_bytes += amount;
    markTouchedLocked();
}

} // namespace vh::storage
