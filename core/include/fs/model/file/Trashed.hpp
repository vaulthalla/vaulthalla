#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include <boost/uuid/uuid.hpp>

namespace pqxx {
class row;
class result;
}

namespace vh::fs::model::file {

struct Trashed {
    unsigned int id{}, vault_id{};
    std::string base32_alias{};
    std::filesystem::path path{}, backing_path{};
    std::time_t trashed_at{}, trashed_by{};
    std::optional<std::time_t> deleted_at{};
    uint64_t size_bytes{};

    explicit Trashed(const pqxx::row& row);
};

std::vector<std::shared_ptr<Trashed>> trashed_files_from_pq_res(const pqxx::result& res);

}
