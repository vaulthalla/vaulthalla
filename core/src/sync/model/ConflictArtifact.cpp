#include "sync/model/ConflictArtifact.hpp"

#include <pqxx/result>
#include <nlohmann/json.hpp>

using namespace vh::sync::model;

ConflictArtifact::ConflictArtifact(const pqxx::result& res) {
    if (res.empty()) throw std::runtime_error("ConflictArtifact: No data found in the result set.");
    if (res.size() != 2) throw std::runtime_error("ConflictArtifact: Expected exactly 2 rows in the result set.");

    for (const auto& row : res) {
        if (const auto side = row["side"].as<std::string>(); !side.empty()) {
            if (side == "local") local = Artifact(row);
            else if (side == "upstream" || side == "remote") upstream = Artifact(row);
            else throw std::runtime_error("ConflictArtifact: Invalid side value in the result set.");
        } else throw std::runtime_error("ConflictArtifact: Missing 'side' column in the result set.");
    }
}

void vh::sync::model::to_json(nlohmann::json& j, const ConflictArtifact& conflict) {
    j = {
        {"local", conflict.local},
        {"upstream", conflict.upstream}
    };
}
