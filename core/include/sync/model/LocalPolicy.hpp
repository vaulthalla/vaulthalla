#pragma once

#include "sync/model/Policy.hpp"

#include <string>
#include <nlohmann/json_fwd.hpp>

namespace pqxx {
class row;
}

namespace vh::sync::model {

struct LocalPolicy final : public Policy {
    enum class ConflictPolicy { Overwrite, KeepBoth, Ask };

    ConflictPolicy conflict_policy{ConflictPolicy::KeepBoth};

    LocalPolicy() = default;
    ~LocalPolicy() override = default;
    explicit LocalPolicy(const pqxx::row& row);

    void rehash_config() override;
    [[nodiscard]] bool resolve_conflict(const std::shared_ptr<Conflict>& conflict) const override;
};

void to_json(nlohmann::json& j, const LocalPolicy& s);
void from_json(const nlohmann::json& j, LocalPolicy& s);

std::string to_string(const LocalPolicy::ConflictPolicy& cp);

LocalPolicy::ConflictPolicy fsConflictPolicyFromString(const std::string& str);

std::string to_string(const std::shared_ptr<LocalPolicy>& sync);

}
