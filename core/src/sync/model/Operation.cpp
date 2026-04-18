#include "sync/model/Operation.hpp"
#include "fs/model/Entry.hpp"
#include "db/encoding/timestamp.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>

using namespace vh::sync::model;
using namespace vh::fs::model;
using namespace vh::db::encoding;

Operation::Operation(const pqxx::row& row)
    : id(row["id"].as<unsigned int>()),
      fs_entry_id(row["fs_entry_id"].as<unsigned int>()),
      executed_by(row["executed_by"].as<unsigned int>()),
      operation(to_op(row["operation"].as<std::string>())),
      target(to_target(row["target"].as<std::string>())),
      status(to_status(row["status"].as<std::string>())),
      source_path(row["source_path"].as<std::string>()),
      destination_path(row["destination_path"].as<std::string>()),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      error(row["error"].as<std::optional<std::string>>()) {
    if (!row["completed_at"].is_null()) completed_at = parsePostgresTimestamp(row["completed_at"].as<std::string>());
}

Operation::Operation(const std::shared_ptr<Entry>& origEntry, const std::filesystem::path& dest, const unsigned int userId, const Op& op)
    : fs_entry_id(origEntry->id),
      executed_by(userId),
      operation(op),
      target(origEntry->isDirectory() ? Target::Directory : Target::File),
      source_path(origEntry->path),
      destination_path(dest) {}

Throughput::Metric Operation::opToThroughputMetric() const {
    if (operation == Op::Copy) return Throughput::Metric::COPY;
    if (operation == Op::Rename || operation == Op::Move) return Throughput::Metric::RENAME;
    throw std::logic_error("Unsupported operation for throughput metric");
}

std::string vh::sync::model::to_string(const Operation::Op& op) {
    switch (op) {
        case Operation::Op::Copy: return "copy";
        case Operation::Op::Move: return "move";
        case Operation::Op::Rename: return "rename";
        default: return "unknown";
    }
}

std::string vh::sync::model::to_string(const Operation::Target& target) {
    switch (target) {
        case Operation::Target::File: return "file";
        case Operation::Target::Directory: return "directory";
        default: return "unknown";
    }
}

std::string vh::sync::model::to_string(const Operation::Status& status) {
    switch (status) {
        case Operation::Status::Pending: return "pending";
        case Operation::Status::InProgress: return "in_progress";
        case Operation::Status::Success: return "success";
        case Operation::Status::Failed: return "failed";
        case Operation::Status::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

Operation::Op vh::sync::model::to_op(const std::string& str) {
    if (str == "copy") return Operation::Op::Copy;
    if (str == "move") return Operation::Op::Move;
    if (str == "rename") return Operation::Op::Rename;
    throw std::invalid_argument("Invalid operation string: " + str);
}

Operation::Target vh::sync::model::to_target(const std::string& str) {
    if (str == "file") return Operation::Target::File;
    if (str == "directory") return Operation::Target::Directory;
    throw std::invalid_argument("Invalid target string: " + str);
}

Operation::Status vh::sync::model::to_status(const std::string& str) {
    if (str == "pending") return Operation::Status::Pending;
    if (str == "in_progress") return Operation::Status::InProgress;
    if (str == "success") return Operation::Status::Success;
    if (str == "failed") return Operation::Status::Failed;
    if (str == "cancelled") return Operation::Status::Cancelled;
    throw std::invalid_argument("Invalid status string: " + str);
}

void vh::sync::model::to_json(nlohmann::json& j, const std::shared_ptr<Operation>& op) {
    j = nlohmann::json{
        {"id", op->id},
        {"fs_entry_id", op->fs_entry_id},
        {"executed_by", op->executed_by},
        {"operation", to_string(op->operation)},
        {"target", to_string(op->target)},
        {"status", to_string(op->status)},
        {"source_path", op->source_path},
        {"destination_path", op->destination_path},
        {"created_at", timestampToString(op->created_at)},
        {"completed_at", timestampToString(op->completed_at)},
        {"error", op->error}
    };
}

void vh::sync::model::from_json(const nlohmann::json& j, std::shared_ptr<Operation>& op) {
    op = std::make_shared<Operation>();
    op->id = j.at("id").get<unsigned int>();
    op->fs_entry_id = j.at("fs_entry_id").get<unsigned int>();
    op->executed_by = j.at("executed_by").get<unsigned int>();
    op->operation = to_op(j.at("operation").get<std::string>());
    op->target = to_target(j.at("target").get<std::string>());
    op->status = to_status(j.at("status").get<std::string>());
    op->source_path = j.at("source_path").get<std::string>();
    op->destination_path = j.at("destination_path").get<std::string>();
    op->created_at = parseTimestampFromString(j.at("created_at").get<std::string>());
    op->completed_at = parseTimestampFromString(j.at("completed_at").get<std::string>());
    if (j.contains("error") && !j["error"].is_null()) {
        op->error = j.at("error").get<std::string>();
    } else {
        op->error.reset();
    }
}

std::vector<std::shared_ptr<Operation>> vh::sync::model::operations_from_pq_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Operation>> operations;
    operations.reserve(res.size());
    for (const auto& row : res) operations.emplace_back(std::make_shared<Operation>(row));
    return operations;
}
