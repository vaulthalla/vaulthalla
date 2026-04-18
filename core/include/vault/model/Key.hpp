#pragma once

#include <vector>
#include <cstdint>
#include <ctime>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
    class row;
}

namespace vh::vault::model {

struct Key {
    unsigned int vaultId{}, version{};
    std::vector<uint8_t> key, encrypted_key, iv;
    std::time_t created_at{}, updated_at{};

    Key() = default;
    explicit Key(const pqxx::row& row);
};

void to_json(nlohmann::json &j, const Key &vk);
void from_json(const nlohmann::json &j, Key &vk);

}
