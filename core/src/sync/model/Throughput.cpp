#include "sync/model/Throughput.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>

using namespace vh::sync::model;
using namespace std::chrono;

Throughput::Throughput(const pqxx::row& row) :
    id(row["id"].as<uint32_t>()),
    run_uuid(row["run_uuid"].as<std::string>()),
    num_ops(row["num_ops"].as<uint64_t>()),
    failed_ops(row["failed_ops"].as<uint64_t>()),
    size_bytes(row["size_bytes"].as<uint64_t>()),
    duration_ms(row["duration_ms"].as<uint64_t>()) {}

void Throughput::computeDashboardStats() {
    num_ops = scoped_ops.size();
    failed_ops = 0;
    size_bytes = 0;
    duration_ms = 0;
    for (const auto& op : scoped_ops) {
        if (!op.success) ++failed_ops;
        else {
            size_bytes += op.size_bytes;
            duration_ms += op.duration_ms();
        }
    }
}

ScopedOp& Throughput::newOp() {
    scoped_ops.emplace_back();
    return scoped_ops.back();
}

void Throughput::parseMetric(const std::string& str) {
    if (str == "rename") metric_type = RENAME;
    else if (str == "copy") metric_type = COPY;
    else if (str == "delete") metric_type = DELETE;
    else if (str == "upload") metric_type = UPLOAD;
    else if (str == "download") metric_type = DOWNLOAD;
}

std::string Throughput::metricToString() const {
    switch (metric_type) {
        case RENAME: return "rename";
        case COPY: return "copy";
        case DELETE: return "delete";
        case UPLOAD: return "upload";
        case DOWNLOAD: return "download";
        default: return "unknown";
    }
}

void vh::sync::model::to_json(nlohmann::json& j, const std::unique_ptr<Throughput>& t) {
    t->computeDashboardStats();
    j = {
        {"id", t->id},
        {"run_uuid", t->run_uuid},
        {"num_ops", t->num_ops},
        {"size_bytes", t->size_bytes},
        {"duration_ms", t->duration_ms},
        {"metric_type", t->metricToString()}
    };
}

void vh::sync::model::to_json(nlohmann::json& j, const std::vector<std::unique_ptr<Throughput>>& t) {
    j = nlohmann::json::array();
    for (const auto& item : t) j.push_back(item);
}
