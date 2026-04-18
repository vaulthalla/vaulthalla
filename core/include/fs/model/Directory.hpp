#pragma once

#include "Entry.hpp"
#include <vector>

namespace vh::fs::model {
    struct Directory final : Entry {
        unsigned int file_count{0};
        unsigned int subdirectory_count{0};

        Directory() = default;

        Directory(const pqxx::row &row, const pqxx::result &parentRows);

        [[nodiscard]] bool isDirectory() const override { return true; }
    };

    void to_json(nlohmann::json &j, const Directory &d);

    void from_json(const nlohmann::json &j, Directory &d);

    std::vector<std::shared_ptr<Directory> > directories_from_pq_res(const pqxx::result &res);
}
