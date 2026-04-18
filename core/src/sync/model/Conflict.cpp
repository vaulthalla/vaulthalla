#include "sync/model/Conflict.hpp"
#include "fs/model/File.hpp"
#include "db/encoding/timestamp.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>
#include <pqxx/result>

using namespace vh::sync::model;
using namespace vh::fs::model;
using namespace vh::db::encoding;

Conflict::Conflict(const pqxx::row& row, const pqxx::result& artifactRows, const pqxx::result& reasonRows)
    : id(row["id"].as<uint32_t>()),
      event_id(row["event_id"].as<uint32_t>()),
      file_id(row["file_id"].as<uint32_t>()),
      resolved_at(parsePostgresTimestamp(row["resolved_at"].as<std::string>())),
      created_at(parsePostgresTimestamp(row["created_at"].as<std::string>())),
      artifacts(artifactRows) {
    parseType(row["type"].as<std::string>());
    parseResolution(row["resolution"].as<std::string>());
    for (const auto& reasonRow : reasonRows) reasons.emplace_back(reasonRow);
}

Conflict::Reason::Reason(const pqxx::row& row)
    : id(row["id"].as<uint32_t>()),
      conflict_id(row["conflict_id"].as<uint32_t>()),
      code(row["code"].as<std::string>()),
      message(row["message"].as<std::string>()) {}

void Conflict::analyze() {
    reasons.clear();
    type = Type::MISMATCH;

    const auto& local    = artifacts.local.file;
    const auto& upstream = artifacts.upstream.file;

    if (local->content_hash && upstream->content_hash &&
        *local->content_hash != *upstream->content_hash)
        reasons.emplace_back("hash_mismatch", "Content hashes differ.");

    if (local->size_bytes != upstream->size_bytes)
        reasons.emplace_back("size_mismatch", "File sizes differ.");


    if (failed_to_decrypt_upstream) {
        type = reasons.empty() ? Type::ENCRYPTION : Type::BOTH;

        if (upstream->encrypted_with_key_version <= 0)
            reasons.emplace_back("upstream_bad_key_version", "Upstream key version is invalid.");

        if (upstream->encryption_iv.empty())
            reasons.emplace_back("upstream_no_iv", "Missing upstream encryption IV.");
    }
}

std::string Conflict::typeToString() const {
    switch (type) {
        case Type::MISMATCH: return "mismatch";
        case Type::ENCRYPTION: return "encryption";
        case Type::BOTH: return "both";
    }

    return "unknown";
}

std::string Conflict::resolutionToString() const {
    switch (resolution) {
        case Resolution::UNRESOLVED: return "unresolved";
        case Resolution::KEPT_LOCAL: return "kept_local";
        case Resolution::KEPT_REMOTE: return "kept_remote";
        case Resolution::KEPT_BOTH: return "kept_both";
        case Resolution::OVERWRITTEN: return "overwritten";
        case Resolution::FIXED_REMOTE_ENCRYPTION: return "fixed_remote_encryption";
    }

    return "unknown";
}

void Conflict::parseResolution(const std::string& resolutionStr) {
    if (resolutionStr == "unresolved") resolution = Resolution::UNRESOLVED;
    else if (resolutionStr == "kept_local") resolution = Resolution::KEPT_LOCAL;
    else if (resolutionStr == "kept_remote") resolution = Resolution::KEPT_REMOTE;
    else if (resolutionStr == "kept_both") resolution = Resolution::KEPT_BOTH;
    else if (resolutionStr == "overwritten") resolution = Resolution::OVERWRITTEN;
    else if (resolutionStr == "fixed_remote_encryption") resolution = Resolution::FIXED_REMOTE_ENCRYPTION;
    else resolution = Resolution::UNRESOLVED; // Default to unresolved if unknown
}

void Conflict::parseType(const std::string& typeStr) {
    if (typeStr == "mismatch") type = Type::MISMATCH;
    else if (typeStr == "encryption") type = Type::ENCRYPTION;
    else if (typeStr == "both") type = Type::BOTH;
    else type = Type::MISMATCH; // Default to mismatch if unknown
}

void vh::sync::model::to_json(nlohmann::json& j, const std::shared_ptr<Conflict>& conflict) {
    j = {
        {"id", conflict->id},
        {"event_id", conflict->event_id},
        {"file_id", conflict->file_id},
        {"type", conflict->typeToString()},
        {"resolution", conflict->resolutionToString()},
        {"resolved_at", conflict->resolved_at},
        {"created_at", conflict->created_at},
        {"artifacts", conflict->artifacts},
        {"reasons", conflict->reasons}
    };
}

void vh::sync::model::to_json(nlohmann::json& j, const Conflict::Reason& reason) {
    j = {
        {"code", reason.code},
        {"message", reason.message}
    };
}

void vh::sync::model::to_json(nlohmann::json& j, const std::vector<Conflict::Reason>& reasons) {
    j = nlohmann::json::array();
    for (const auto& reason : reasons) j.push_back(reason);
}

void vh::sync::model::to_json(nlohmann::json& j, const std::vector<std::shared_ptr<Conflict>>& conflicts) {
    j = nlohmann::json::array();
    for (const auto& conflict : conflicts) j.push_back(conflict);
}

