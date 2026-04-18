#pragma once

#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::vault::model {

struct Capacity;

struct Stat {
    uint32_t vault_id;
    std::shared_ptr<Capacity> capacity;

    explicit Stat(unsigned int vaultId);
};

void to_json(nlohmann::json& j, const std::shared_ptr<Stat>& s);

}
