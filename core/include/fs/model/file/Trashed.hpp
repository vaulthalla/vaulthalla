#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include <boost/uuid/uuid.hpp>
#include "db/Fwd.hpp"

namespace vh::fs::model::file {

struct Trashed {
    unsigned int id{}, vault_id{};
    std::string base32_alias{};
    std::filesystem::path path{}, backing_path{};
    std::time_t trashed_at{}, trashed_by{};
    std::optional<std::time_t> deleted_at{};
    uint64_t size_bytes{};

    Trashed() = default;
    explicit Trashed(pqxx::row_ref row);
};
}
