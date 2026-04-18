#pragma once

#include <chrono>
#include <string>
#include <ctime>
#include <pqxx/row>
#include <nlohmann/json_fwd.hpp>

namespace vh::sync::model {

struct Conflict;

struct Policy {
    unsigned int id{}, vault_id{};
    std::chrono::seconds interval{};
    bool enabled{};
    std::time_t last_sync_at{}, last_success_at{}, created_at{}, updated_at{};
    std::string config_hash{};

    Policy() = default;
    virtual ~Policy() = default;
    explicit Policy(const pqxx::row& row);

    virtual void rehash_config() = 0;
    [[nodiscard]] virtual bool resolve_conflict(const std::shared_ptr<Conflict>& conflict) const = 0;
};

void to_json(nlohmann::json& j, const Policy& s);
void from_json(const nlohmann::json& j, Policy& s);

std::string to_string(const std::shared_ptr<Policy>& sync);

}
