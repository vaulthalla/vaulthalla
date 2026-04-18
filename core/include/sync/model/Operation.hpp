#pragma once

#include "sync/model/Throughput.hpp"

#include <string>
#include <optional>
#include <ctime>
#include <filesystem>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
class row;
class result;
}

namespace vh::fs::model {
struct Entry;
}

namespace vh::sync::model {

struct Operation {
    enum class Op { Copy, Move, Rename };
    enum class Target { File, Directory };
    enum class Status { Pending, InProgress, Success, Failed, Cancelled };

    unsigned int id{}, fs_entry_id{}, executed_by{};
    Op operation{Op::Rename};
    Target target{Target::File};
    Status status{Status::Pending};
    std::string source_path, destination_path;
    std::time_t created_at{}, completed_at{};
    std::optional<std::string> error;

    Operation() = default;
    explicit Operation(const pqxx::row& row);
    explicit Operation(const std::shared_ptr<fs::model::Entry>& origEntry, const std::filesystem::path& dest, unsigned int userId, const Op& op);

    [[nodiscard]] Throughput::Metric opToThroughputMetric() const;
};

std::string to_string(const Operation::Op& op);
std::string to_string(const Operation::Target& target);
std::string to_string(const Operation::Status& status);

Operation::Op to_op(const std::string& str);
Operation::Target to_target(const std::string& str);
Operation::Status to_status(const std::string& str);

void to_json(nlohmann::json& j, const std::shared_ptr<Operation>& op);
void from_json(const nlohmann::json& j, std::shared_ptr<Operation>& op);

std::vector<std::shared_ptr<Operation>> operations_from_pq_res(const pqxx::result& res);

}
