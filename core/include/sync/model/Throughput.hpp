#pragma once

#include "sync/model/ScopedOp.hpp"

#include <ctime>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::sync::model {

struct Throughput {
    enum Metric {
        UPLOAD,
        DOWNLOAD,
        INDEX,
        RENAME,
        COPY,
        DELETE
    };

    uint32_t id{};
    std::string run_uuid{};

    Metric metric_type{RENAME};

    uint64_t num_ops{};
    uint64_t failed_ops{};
    uint64_t size_bytes{};
    uint64_t duration_ms{};

    std::vector<std::shared_ptr<ScopedOp>> scoped_ops;

    Throughput() = default;
    explicit Throughput(const pqxx::row& row);

    void computeDashboardStats();

    std::shared_ptr<ScopedOp> newOp();

    void parseMetric(const std::string& str);
    [[nodiscard]] std::string metricToString() const;
};

void to_json(nlohmann::json& j, const std::unique_ptr<Throughput>& t);
void to_json(nlohmann::json& j, const std::vector<std::unique_ptr<Throughput>>& t);

}
