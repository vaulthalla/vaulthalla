#pragma once

#include <cstdint>
#include <string>

namespace vh::storage::s3 {

struct S3GatewayUpstreamUsage {
    uint64_t list_requests = 0;
    uint64_t head_requests = 0;
    uint64_t get_requests = 0;
    uint64_t put_requests = 0;
    uint64_t copy_requests = 0;
    uint64_t delete_requests = 0;
    uint64_t downloaded_bytes = 0;
    uint64_t uploaded_bytes = 0;
    bool touched_upstream = false;
    bool synthetic = false;
    std::string source;

    [[nodiscard]] bool empty() const noexcept {
        return list_requests == 0 &&
            head_requests == 0 &&
            get_requests == 0 &&
            put_requests == 0 &&
            copy_requests == 0 &&
            delete_requests == 0 &&
            downloaded_bytes == 0 &&
            uploaded_bytes == 0;
    }

    void merge(const S3GatewayUpstreamUsage& other) {
        list_requests += other.list_requests;
        head_requests += other.head_requests;
        get_requests += other.get_requests;
        put_requests += other.put_requests;
        copy_requests += other.copy_requests;
        delete_requests += other.delete_requests;
        downloaded_bytes += other.downloaded_bytes;
        uploaded_bytes += other.uploaded_bytes;
        touched_upstream = touched_upstream || other.touched_upstream;
        synthetic = synthetic || other.synthetic;
        if (source.empty()) source = other.source;
    }
};

template <typename T>
struct GatewayOperationResult {
    T value;
    S3GatewayUpstreamUsage actual_upstream_usage;
    S3GatewayUpstreamUsage effective_gateway_usage;
};

} // namespace vh::storage::s3
