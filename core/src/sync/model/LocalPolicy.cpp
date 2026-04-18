#include "sync/model/LocalPolicy.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/interval.hpp"
#include "sync/model/Conflict.hpp"
#include "fs/model/File.hpp"

#include <pqxx/row>
#include <nlohmann/json.hpp>

using namespace vh::sync::model;
using namespace vh::fs::model;
using namespace vh::db::encoding;

LocalPolicy::LocalPolicy(const pqxx::row& row)
    : Policy(row),
      conflict_policy(fsConflictPolicyFromString(row.at("conflict_policy").as<std::string>())) {
    rehash_config();
}

void LocalPolicy::rehash_config() {
    config_hash = "vault_id=" + std::to_string(vault_id) +
                  ";interval=" + std::to_string(interval.count()) +
                  ";enabled=" + (enabled ? "true" : "false") +
                  ";conflict_policy=" + to_string(conflict_policy);
}

bool LocalPolicy::resolve_conflict(const std::shared_ptr<Conflict>& conflict) const {
    if (conflict_policy == ConflictPolicy::Ask) return false; // Let the user decide
    
    if (conflict_policy == ConflictPolicy::KeepBoth) conflict->resolution = Conflict::Resolution::KEPT_BOTH;
    else if (conflict_policy == ConflictPolicy::Overwrite) conflict->resolution = Conflict::Resolution::OVERWRITTEN;

    return true;
}

void vh::sync::model::to_json(nlohmann::json& j, const LocalPolicy& s) {
    to_json(j, static_cast<const Policy&>(s));
    j["conflict_policy"] = to_string(s.conflict_policy);
}

void vh::sync::model::from_json(const nlohmann::json& j, LocalPolicy& s) {
    from_json(j, static_cast<Policy&>(s));
    s.conflict_policy = fsConflictPolicyFromString(j.at("conflict_policy").get<std::string>());
}

std::string vh::sync::model::to_string(const LocalPolicy::ConflictPolicy& cp) {
    switch (cp) {
        case LocalPolicy::ConflictPolicy::Overwrite: return "overwrite";
        case LocalPolicy::ConflictPolicy::KeepBoth: return "keep_both";
        case LocalPolicy::ConflictPolicy::Ask: return "ask";
        default: throw std::invalid_argument("Unknown conflict policy");
    }
}

LocalPolicy::ConflictPolicy vh::sync::model::fsConflictPolicyFromString(const std::string& str) {
    if (str == "overwrite") return LocalPolicy::ConflictPolicy::Overwrite;
    if (str == "keep_both") return LocalPolicy::ConflictPolicy::KeepBoth;
    if (str == "ask") return LocalPolicy::ConflictPolicy::Ask;
    throw std::invalid_argument("Unknown conflict policy: " + str);
}

std::string vh::sync::model::to_string(const std::shared_ptr<LocalPolicy>& sync) {
    if (!sync) return "null";
    return "Local Vault Sync Configuration:\n"
           "  Vault ID: " + std::to_string(sync->vault_id) + "\n"
           "  Interval: " + intervalToString(sync->interval) + "\n"
           "  Enabled: " + (sync->enabled ? "true" : "false") + "\n"
           "  Conflict Policy: " + to_string(sync->conflict_policy) + "\n"
           "  Last Sync At: " + timestampToString(sync->last_sync_at) + "\n"
           "  Last Success At: " + timestampToString(sync->last_success_at) + "\n"
           "  Created At: " + timestampToString(sync->created_at) + "\n"
           "  Updated At: " + timestampToString(sync->updated_at);
}
