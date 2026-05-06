#include "stats/model/VaultShareStats.hpp"

#include <nlohmann/json.hpp>

namespace vh::stats::model {

namespace {

template <typename T>
nlohmann::json nullableShareValue(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

}

void to_json(nlohmann::json& j, const VaultShareTopLink& link) {
    j = nlohmann::json{
        {"share_id", link.shareId},
        {"label", link.label},
        {"root_path", link.rootPath},
        {"link_type", link.linkType},
        {"access_mode", link.accessMode},
        {"access_count", link.accessCount},
        {"download_count", link.downloadCount},
        {"upload_count", link.uploadCount},
        {"last_accessed_at", nullableShareValue(link.lastAccessedAt)},
    };
}

void to_json(nlohmann::json& j, const VaultShareRecentEvent& event) {
    j = nlohmann::json{
        {"id", event.id},
        {"share_id", nullableShareValue(event.shareId)},
        {"event_type", event.eventType},
        {"status", event.status},
        {"target_path", nullableShareValue(event.targetPath)},
        {"bytes_transferred", event.bytesTransferred},
        {"error_code", nullableShareValue(event.errorCode)},
        {"error_message", nullableShareValue(event.errorMessage)},
        {"created_at", event.createdAt},
    };
}

void to_json(nlohmann::json& j, const VaultShareStats& stats) {
    j = nlohmann::json{
        {"vault_id", stats.vaultId},
        {"active_links", stats.activeLinks},
        {"expired_links", stats.expiredLinks},
        {"revoked_links", stats.revokedLinks},
        {"links_created_24h", stats.linksCreated24h},
        {"links_revoked_24h", stats.linksRevoked24h},
        {"public_links", stats.publicLinks},
        {"email_validated_links", stats.emailValidatedLinks},
        {"downloads_24h", stats.downloads24h},
        {"uploads_24h", stats.uploads24h},
        {"denied_attempts_24h", stats.deniedAttempts24h},
        {"rate_limited_attempts_24h", stats.rateLimitedAttempts24h},
        {"failed_attempts_24h", stats.failedAttempts24h},
        {"top_links_by_access", stats.topLinksByAccess},
        {"recent_share_events", stats.recentShareEvents},
        {"checked_at", stats.checkedAt},
    };
}

}
