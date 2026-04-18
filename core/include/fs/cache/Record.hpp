#pragma once

#include <filesystem>
#include <ctime>
#include <memory>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace pqxx {
class row;
class result;
}

namespace vh::fs::cache {

struct Record {
    enum class Type { File, Thumbnail };

    unsigned int id{}, vault_id{}, file_id{};
    std::filesystem::path path{};
    Type type{Type::Thumbnail};
    uintmax_t size{};
    std::time_t last_accessed{}, created_at{};

    Record() = default;
    explicit Record(const pqxx::row& row);
};

void to_json(nlohmann::json& j, const Record& index);
void from_json(const nlohmann::json& j, Record& index);

std::string to_string(const Record::Type& type);
Record::Type typeFromString(const std::string& str);

std::vector<std::shared_ptr<Record>> cache_indices_from_pq_res(const pqxx::result& res);

}
