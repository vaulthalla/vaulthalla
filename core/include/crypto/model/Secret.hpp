#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <cstdint>
#include <pqxx/types>

namespace vh::crypto::model {

struct Secret {
    std::string key;
    std::vector<uint8_t> value, iv;
    std::time_t created_at{std::time(nullptr)}, updated_at{std::time(nullptr)};

    Secret() = default;
    explicit Secret(const pqxx::row& row);
};

}
