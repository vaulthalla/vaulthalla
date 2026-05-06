#include "stats/model/VaultActivity.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullable(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void to_json(nlohmann::json& j, const VaultActivityTopUser& user) {
    j = nlohmann::json{
        {"user_id", nullable(user.userId)},
        {"user_name", nullable(user.userName)},
        {"count", user.count},
    };
}

void to_json(nlohmann::json& j, const VaultActivityTopPath& path) {
    j = nlohmann::json{
        {"path", path.path},
        {"action", path.action},
        {"count", path.count},
        {"bytes", path.bytes},
    };
}

void to_json(nlohmann::json& j, const VaultActivityEvent& event) {
    j = nlohmann::json{
        {"source", event.source},
        {"action", event.action},
        {"path", event.path},
        {"user_id", nullable(event.userId)},
        {"user_name", nullable(event.userName)},
        {"status", nullable(event.status)},
        {"error", nullable(event.error)},
        {"bytes", event.bytes},
        {"occurred_at", event.occurredAt},
    };
}

void to_json(nlohmann::json& j, const VaultActivity& activity) {
    j = nlohmann::json{
        {"vault_id", activity.vaultId},
        {"last_activity_at", nullable(activity.lastActivityAt)},
        {"last_activity_action", nullable(activity.lastActivityAction)},
        {"uploads_24h", activity.uploads24h},
        {"uploads_7d", activity.uploads7d},
        {"deletes_24h", activity.deletes24h},
        {"deletes_7d", activity.deletes7d},
        {"renames_24h", activity.renames24h},
        {"renames_7d", activity.renames7d},
        {"moves_24h", activity.moves24h},
        {"moves_7d", activity.moves7d},
        {"copies_24h", activity.copies24h},
        {"copies_7d", activity.copies7d},
        {"restores_24h", activity.restores24h},
        {"restores_7d", activity.restores7d},
        {"bytes_added_24h", activity.bytesAdded24h},
        {"bytes_removed_24h", activity.bytesRemoved24h},
        {"top_active_users", activity.topActiveUsers},
        {"top_touched_paths", activity.topTouchedPaths},
        {"recent_activity", activity.recentActivity},
        {"checked_at", activity.checkedAt},
    };
}

}
