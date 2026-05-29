#include "protocols/ws/handler/Pricing.hpp"

#include "db/query/fs/File.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "protocols/ws/Session.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/permission/vault/sync/Action.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "runtime/Deps.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "storage/s3/pricing/PriceEstimate.hpp"
#include "sync/Cloud.hpp"
#include "sync/Planner.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>

namespace vh::protocols::ws::handler {
namespace {

using vh::storage::s3::pricing::PriceBudgetMode;
using vh::storage::s3::pricing::PriceBudgetPolicy;
using vh::storage::s3::pricing::PriceBudgetScope;
using vh::storage::s3::pricing::PriceBudgetService;
using vh::storage::s3::pricing::priceBudgetModeFromString;
using vh::storage::s3::pricing::priceBudgetScopeFromString;

std::optional<std::uint32_t> optionalVaultId(const json& payload) {
    if (!payload.is_object() || !payload.contains("vault_id") || payload.at("vault_id").is_null()) return std::nullopt;
    return payload.at("vault_id").get<std::uint32_t>();
}

std::uint32_t limitFromPayload(const json& payload, const std::uint32_t fallback = 50) {
    if (!payload.is_object()) return fallback;
    return std::clamp<std::uint32_t>(payload.value("limit", fallback), 1, 500);
}

bool canViewVaultBudget(const std::shared_ptr<Session>& session, const std::uint32_t vaultId) {
    return vh::rbac::resolver::Admin::has<vh::rbac::permission::admin::VaultPermissions>({
        .user = session->user,
        .permissions = {
            vh::rbac::permission::admin::VaultPermissions::View,
            vh::rbac::permission::admin::VaultPermissions::ViewStats
        },
        .vault_id = vaultId
    });
}

bool canEditVaultBudget(const std::shared_ptr<Session>& session, const std::uint32_t vaultId) {
    return vh::rbac::resolver::Admin::has<vh::rbac::permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = vh::rbac::permission::admin::VaultPermissions::Edit,
        .vault_id = vaultId
    });
}

void requireVaultBudgetView(const std::shared_ptr<Session>& session, const std::uint32_t vaultId) {
    if (!canViewVaultBudget(session, vaultId))
        throw std::runtime_error("You do not have permission to view S3 price budget data for this vault.");
}

void requireSuperAdmin(const std::shared_ptr<Session>& session, const char* message) {
    if (!session->user || !session->user->isSuperAdmin()) throw std::runtime_error(message);
}

bool policyVisibleTo(const PriceBudgetPolicy& policy, const std::shared_ptr<Session>& session, const std::optional<std::uint32_t>& scopedVaultId) {
    if (session->user->isSuperAdmin()) return true;
    if (policy.vault_id) return canViewVaultBudget(session, *policy.vault_id);
    if (scopedVaultId) return canViewVaultBudget(session, *scopedVaultId);
    return false;
}

std::vector<PriceBudgetPolicy> visiblePolicies(const json& payload, const std::shared_ptr<Session>& session) {
    const auto scopedVaultId = optionalVaultId(payload);
    if (scopedVaultId) requireVaultBudgetView(session, *scopedVaultId);

    auto policies = PriceBudgetService{}.listPolicies(payload.value("include_inactive", true));
    std::erase_if(policies, [&](const auto& policy) {
        if (!policyVisibleTo(policy, session, scopedVaultId)) return true;
        if (!scopedVaultId) return false;
        return policy.scope == PriceBudgetScope::Vault && policy.vault_id && *policy.vault_id != *scopedVaultId;
    });
    return policies;
}

std::optional<std::string> optionalStringPayload(const json& payload, const char* key) {
    if (!payload.contains(key) || payload.at(key).is_null()) return std::nullopt;
    const auto value = payload.at(key).get<std::string>();
    return value.empty() ? std::optional<std::string>{} : std::make_optional(value);
}

std::optional<std::uint32_t> optionalUIntPayload(const json& payload, const char* key) {
    if (!payload.contains(key) || payload.at(key).is_null()) return std::nullopt;
    return payload.at(key).get<std::uint32_t>();
}

PriceBudgetPolicy policyFromPayload(const json& payload) {
    PriceBudgetPolicy policy;
    policy.scope = priceBudgetScopeFromString(payload.at("scope").get<std::string>());
    policy.provider_key = optionalStringPayload(payload, "provider_key");
    policy.vault_id = optionalUIntPayload(payload, "vault_id");
    policy.mode = priceBudgetModeFromString(payload.value("mode", "report"));
    policy.currency = payload.value("currency", "USD");
    policy.max_run_cost = optionalStringPayload(payload, "max_run_cost");
    policy.max_daily_cost = optionalStringPayload(payload, "max_daily_cost");
    policy.max_monthly_cost = optionalStringPayload(payload, "max_monthly_cost");
    policy.require_verified_catalog = payload.value("require_verified_catalog", true);
    policy.allow_stale_catalog = payload.value("allow_stale_catalog", false);
    policy.max_catalog_age_seconds = payload.contains("max_catalog_age_seconds") && !payload.at("max_catalog_age_seconds").is_null()
        ? std::make_optional(payload.at("max_catalog_age_seconds").get<std::int64_t>())
        : std::optional<std::int64_t>{43200};
    return policy;
}

std::vector<std::uint32_t> policyIdsFromPayload(const json& payload) {
    std::vector<std::uint32_t> ids;
    if (!payload.contains("policy_ids") || !payload.at("policy_ids").is_array()) return ids;
    for (const auto& item : payload.at("policy_ids")) ids.push_back(item.get<std::uint32_t>());
    return ids;
}

std::shared_ptr<vh::storage::CloudEngine> requireCloudEngine(const std::uint32_t vaultId) {
    const auto engine = vh::runtime::Deps::get().storageManager->getEngine(vaultId);
    if (!engine) throw std::runtime_error("Vault engine is not available.");
    if (engine->type() != vh::storage::StorageType::Cloud)
        throw std::runtime_error("S3 price budget preflight is only available for S3 vaults.");
    return std::static_pointer_cast<vh::storage::CloudEngine>(engine);
}

json buildPreflight(const json& payload) {
    const auto vaultId = payload.at("vault_id").get<std::uint32_t>();
    const auto cloud = requireCloudEngine(vaultId);
    const auto policy = cloud->remote_policy();
    const auto summary = vh::db::query::sync::RemoteObjectIndex::summaryForVault(vaultId);
    if (summary.object_count == 0)
        throw std::runtime_error("No remote index is available for S3 price budget preflight.");
    if (summary.isStale(policy->max_remote_index_age))
        throw std::runtime_error("Remote index is stale; refresh it before S3 price budget preflight.");

    auto ctx = std::make_shared<vh::sync::Cloud>(cloud);
    ctx->event = std::make_shared<vh::sync::model::Event>();
    ctx->event->vault_id = vaultId;
    ctx->event->run_uuid = payload.value("run_uuid", "web-preflight");
    ctx->localFiles = vh::db::query::fs::File::listFilesInDir(vaultId);
    ctx->localMap = vh::fs::model::groupEntriesByPath(ctx->localFiles);
    ctx->s3Files = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ctx->s3Map = vh::fs::model::groupEntriesByPath(ctx->s3Files);

    vh::sync::model::S3CostEstimate planningNotes;
    const auto plan = vh::sync::Planner::build(ctx, policy, &planningNotes);
    auto estimate = vh::sync::Planner::estimateS3Cost(plan);
    estimate.archive_tier_downloads_skipped = planningNotes.archive_tier_downloads_skipped;
    const auto budgetPriceEstimate = vh::storage::s3::pricing::estimatePlannedS3Sync(
        *cloud,
        estimate,
        {.mode = vh::storage::s3::pricing::PriceEstimateMode::BudgetConservative});

    const auto profile = cloud->s3ProviderProfile();
    const auto costProfileId = profile ? profile->costProfileId() : std::optional<std::string>{};
    const auto providerKey = costProfileId ? *costProfileId : (profile ? profile->id() : std::string{"unknown"});
    const bool providerSupported = costProfileId &&
        vh::storage::s3::pricing::isSupportedPriceBudgetProvider(*costProfileId);

    const auto decision = PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = ctx->event->run_uuid,
        .provider_key = providerKey,
        .provider_supported = providerSupported,
        .estimate = budgetPriceEstimate,
        .dry_run = true,
        .override_policy_ids = {}
    });

    return {
        {"decision", decision},
        {"estimate", budgetPriceEstimate},
        {"plan", {
            {"upload", std::ranges::count_if(plan, [](const auto& action) { return action.type == vh::sync::model::ActionType::Upload; })},
            {"download", std::ranges::count_if(plan, [](const auto& action) { return action.type == vh::sync::model::ActionType::Download; })},
            {"index_remote_only", std::ranges::count_if(plan, [](const auto& action) { return action.type == vh::sync::model::ActionType::IndexRemoteOnly; })},
            {"delete_remote", std::ranges::count_if(plan, [](const auto& action) { return action.type == vh::sync::model::ActionType::DeleteRemote; })},
            {"delete_local", std::ranges::count_if(plan, [](const auto& action) { return action.type == vh::sync::model::ActionType::DeleteLocal; })}
        }}
    };
}

} // namespace

json Pricing::policyList(const json& payload, const std::shared_ptr<Session>& session) {
    return {{"policies", visiblePolicies(payload.is_object() ? payload : json::object(), session)}};
}

json Pricing::policyUpsert(const json& payload, const std::shared_ptr<Session>& session) {
    auto policy = policyFromPayload(payload);
    if (policy.scope == PriceBudgetScope::Global || policy.scope == PriceBudgetScope::Provider) {
        requireSuperAdmin(session, "Only super-admins may change global or provider S3 price budget policies.");
    } else if (!policy.vault_id || !canEditVaultBudget(session, *policy.vault_id)) {
        throw std::runtime_error("You do not have permission to change this vault S3 price budget policy.");
    }
    return {{"policy", PriceBudgetService{}.upsertPolicy(std::move(policy))}};
}

json Pricing::policyDisable(const json& payload, const std::shared_ptr<Session>& session) {
    const auto scope = priceBudgetScopeFromString(payload.at("scope").get<std::string>());
    const auto providerKey = optionalStringPayload(payload, "provider_key");
    const auto vaultId = optionalUIntPayload(payload, "vault_id");
    if (scope == PriceBudgetScope::Global || scope == PriceBudgetScope::Provider) {
        requireSuperAdmin(session, "Only super-admins may disable global or provider S3 price budget policies.");
    } else if (!vaultId || !canEditVaultBudget(session, *vaultId)) {
        throw std::runtime_error("You do not have permission to disable this vault S3 price budget policy.");
    }
    return {{"disabled", PriceBudgetService{}.disablePolicy(scope, providerKey, vaultId)}};
}

json Pricing::ledgerList(const json& payload, const std::shared_ptr<Session>& session) {
    auto vaultId = optionalVaultId(payload);
    if (!session->user->isSuperAdmin()) {
        if (!vaultId) throw std::runtime_error("Vault-scoped ledger access requires vault_id.");
        requireVaultBudgetView(session, *vaultId);
    } else if (vaultId) {
        requireVaultBudgetView(session, *vaultId);
    }
    return {{"ledger", PriceBudgetService{}.listLedger(limitFromPayload(payload), vaultId)}};
}

json Pricing::status(const json& payload, const std::shared_ptr<Session>& session) {
    auto vaultId = optionalVaultId(payload);
    if (vaultId) requireVaultBudgetView(session, *vaultId);
    else if (!session->user->isSuperAdmin()) throw std::runtime_error("System price budget status requires super-admin.");

    PriceBudgetService service;
    service.expireStaleReservations();
    return {
        {"policies", visiblePolicies(payload.is_object() ? payload : json::object(), session)},
        {"ledger", service.listLedger(limitFromPayload(payload, 20), vaultId)},
        {"trends", service.trendStats(vaultId)},
        {"notifications", service.listNotifications(20, vaultId, false)},
        {"overrides", service.listOverrides(20, vaultId, true)}
    };
}

json Pricing::preflight(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<std::uint32_t>();
    requireVaultBudgetView(session, vaultId);
    return buildPreflight(payload);
}

json Pricing::overrideRequest(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("vault_id").get<std::uint32_t>();
    if (!vh::rbac::resolver::Vault::has<vh::rbac::permission::vault::sync::SyncActionPermissions>({
        .user = session->user,
        .permission = vh::rbac::permission::vault::sync::SyncActionPermissions::Trigger,
        .vault_id = vaultId
    })) throw std::runtime_error("You do not have permission to request a budget override for this vault.");

    return {{"override", PriceBudgetService{}.requestOverride({
        .run_uuid = optionalStringPayload(payload, "run_uuid"),
        .vault_id = vaultId,
        .requested_by = session->user->id,
        .reason = optionalStringPayload(payload, "reason"),
        .policy_ids = policyIdsFromPayload(payload),
        .estimated_cost = optionalStringPayload(payload, "estimated_cost"),
        .currency = payload.value("currency", "USD"),
        .ttl_minutes = payload.value("ttl_minutes", 30u)
    })}};
}

json Pricing::overrideApprove(const json& payload, const std::shared_ptr<Session>& session) {
    requireSuperAdmin(session, "Only super-admins may approve S3 price budget overrides.");
    return {{"override", PriceBudgetService{}.approveOverride(payload.at("id").get<std::uint32_t>(), session->user->id)}};
}

json Pricing::overrideDeny(const json& payload, const std::shared_ptr<Session>& session) {
    requireSuperAdmin(session, "Only super-admins may deny S3 price budget overrides.");
    return {{"override", PriceBudgetService{}.denyOverride(
        payload.at("id").get<std::uint32_t>(),
        session->user->id,
        optionalStringPayload(payload, "reason"))}};
}

json Pricing::overrideList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto body = payload.is_object() ? payload : json::object();
    auto vaultId = optionalVaultId(body);
    if (!session->user->isSuperAdmin()) {
        if (!vaultId) throw std::runtime_error("Vault-scoped override access requires vault_id.");
        requireVaultBudgetView(session, *vaultId);
    } else if (vaultId) {
        requireVaultBudgetView(session, *vaultId);
    }
    return {{"overrides", PriceBudgetService{}.listOverrides(limitFromPayload(body), vaultId, body.value("include_expired", false))}};
}

json Pricing::notificationsList(const json& payload, const std::shared_ptr<Session>& session) {
    const auto body = payload.is_object() ? payload : json::object();
    auto vaultId = optionalVaultId(body);
    const auto limit = limitFromPayload(body);
    const auto includeAcknowledged = body.value("include_acknowledged", false);
    PriceBudgetService service;

    if (!session->user->isSuperAdmin()) {
        if (!vaultId) {
            auto notifications = service.listNotifications(500, std::nullopt, includeAcknowledged);
            std::erase_if(notifications, [&](const auto& notification) {
                return !notification.vault_id || !canViewVaultBudget(session, *notification.vault_id);
            });
            if (notifications.size() > limit) notifications.resize(limit);
            return {{"notifications", notifications}};
        }
        requireVaultBudgetView(session, *vaultId);
    } else if (vaultId) {
        requireVaultBudgetView(session, *vaultId);
    }
    return {{"notifications", service.listNotifications(limit, vaultId, includeAcknowledged)}};
}

json Pricing::notificationsAck(const json& payload, const std::shared_ptr<Session>& session) {
    const auto id = payload.at("id").get<std::uint32_t>();
    const auto vaultId = optionalVaultId(payload);
    PriceBudgetService service;

    if (!session->user->isSuperAdmin()) {
        if (!vaultId) throw std::runtime_error("Vault-scoped notification acknowledgement requires vault_id.");
        requireVaultBudgetView(session, *vaultId);
        const auto visible = service.listNotifications(500, vaultId, true);
        if (std::ranges::none_of(visible, [id](const auto& notification) { return notification.id == id; }))
            throw std::runtime_error("Operator notification is not visible for this vault.");
    }

    return {{"notification", service.acknowledgeNotification(id, session->user->id)}};
}

}
