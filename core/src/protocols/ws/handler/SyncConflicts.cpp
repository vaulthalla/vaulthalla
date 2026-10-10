#include "protocols/ws/handler/SyncConflicts.hpp"

#include "ops/Conflicts.hpp"
#include "protocols/ws/Session.hpp"

#include <nlohmann/json.hpp>

namespace vh::protocols::ws::handler {

json SyncConflicts::summary(const std::shared_ptr<Session>& session) {
    return ops::conflicts::summary(session->user);
}

json SyncConflicts::list(const json& payload, const std::shared_ptr<Session>& session) {
    std::optional<uint32_t> vaultId;
    if (payload.is_object() && payload.contains("vault_id") && !payload.at("vault_id").is_null())
        vaultId = payload.at("vault_id").get<uint32_t>();
    const auto conflicts = ops::conflicts::list(session->user, vaultId);
    auto out = json::array();
    for (const auto& view : conflicts) out.push_back(view);
    return {{"conflicts", out}};
}

json SyncConflicts::resolve(const json& payload, const std::shared_ptr<Session>& session) {
    if (!payload.is_object() || !payload.contains("resolution") || !payload.at("resolution").is_string())
        throw ops::Invalid("resolution must be keep_local or keep_remote");
    const auto decision = ops::conflicts::parseDecision(payload.at("resolution").get<std::string>());
    if (!payload.contains("conflict_ids") || !payload.at("conflict_ids").is_array())
        throw ops::Invalid("conflict_ids must be a list of conflict ids");
    std::vector<uint32_t> ids;
    for (const auto& id : payload.at("conflict_ids")) {
        if (!id.is_number_unsigned()) throw ops::Invalid("conflict_ids must be a list of conflict ids");
        ids.push_back(id.get<uint32_t>());
    }
    return ops::conflicts::resolve(session->user, decision, ids);
}

}
