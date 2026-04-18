#pragma once

#include "sync/model/ScopedOp.hpp"

#include <ctime>
#include <cstdint>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace pqxx { class row; }

namespace vh::sync::model {

struct Throughput {
    enum Metric {
        UPLOAD,
        DOWNLOAD,
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

    std::vector<ScopedOp> scoped_ops;

    Throughput() = default;
    explicit Throughput(const pqxx::row& row);

    void computeDashboardStats();

    ScopedOp& newOp();

    void parseMetric(const std::string& str);
    [[nodiscard]] std::string metricToString() const;
};

void to_json(nlohmann::json& j, const std::unique_ptr<Throughput>& t);
void to_json(nlohmann::json& j, const std::vector<std::unique_ptr<Throughput>>& t);

}
