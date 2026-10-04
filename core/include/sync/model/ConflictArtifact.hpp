# pragma once

#include "Artifact.hpp"

#include <nlohmann/json_fwd.hpp>
#include <pqxx/types>

namespace vh::sync::model {

struct ConflictArtifact {
    Artifact local{}, upstream{};

    ConflictArtifact() = default;
    explicit ConflictArtifact(const pqxx::result& res);
};

void to_json(nlohmann::json& j, const ConflictArtifact& conflict);

}
