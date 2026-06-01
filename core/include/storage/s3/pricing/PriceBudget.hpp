#pragma once

#include "storage/s3/pricing/PriceBotModels.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::storage::s3::pricing {

enum class PriceBudgetMode {
    Off,
    Report,
    Warn,
    Enforce
};

enum class PriceBudgetScope {
    Global,
    Provider,
    Vault,
    GatewayCredential,
    GatewayCredentialVault
};

enum class PriceBudgetWindow {
    PerRun,
    Daily,
    Monthly
};

struct PriceBudgetPolicy {
    std::uint32_t id{0};
    PriceBudgetScope scope{PriceBudgetScope::Vault};
    std::optional<std::string> provider_key;
    std::optional<std::uint32_t> vault_id;
    std::optional<std::uint32_t> gateway_credential_id;
    PriceBudgetMode mode{PriceBudgetMode::Off};
    std::string currency{"USD"};
    std::optional<std::string> max_run_cost;
    std::optional<std::string> max_daily_cost;
    std::optional<std::string> max_monthly_cost;
    bool require_verified_catalog{true};
    bool allow_stale_catalog{false};
    std::optional<std::int64_t> max_catalog_age_seconds{43200};
    bool is_active{true};
};

struct PriceBudgetReservation {
    std::uint32_t id{0};
    std::uint32_t policy_id{0};
    PriceBudgetWindow window{PriceBudgetWindow::PerRun};
};

struct PriceBudgetWindowCheck {
    std::uint32_t policy_id{0};
    PriceBudgetScope scope{PriceBudgetScope::Vault};
    PriceBudgetMode mode{PriceBudgetMode::Off};
    PriceBudgetWindow window{PriceBudgetWindow::PerRun};
    std::string currency{"USD"};
    std::optional<std::string> limit;
    std::string used_before{"0.00000000"};
    std::string remaining_before{"0.00000000"};
    std::string requested{"0.00000000"};
    bool exceeded{false};
};

struct PriceBudgetDecision {
    bool allowed{true};
    bool stalled{false};
    std::vector<std::string> warnings;
    std::optional<std::uint32_t> exceeded_policy_id;
    std::string exceeded_scope;
    std::string limit;
    std::string remaining_before;
    std::string requested;
    std::string currency;
    std::string reason;
    std::vector<PriceBudgetPolicy> policies;
    std::vector<PriceBudgetWindowCheck> checks;
    std::vector<PriceBudgetReservation> reservations;
};

struct PriceBudgetPreflightRequest {
    std::uint32_t vault_id{0};
    std::string run_uuid;
    std::string provider_key;
    bool provider_supported{false};
    PriceEstimateReport estimate;
    bool dry_run{false};
    std::vector<std::uint32_t> override_policy_ids;
    std::optional<std::uint32_t> gateway_credential_id;
    std::string request_uuid;
    std::string operation;
    std::optional<std::string> object_key;
};

struct PriceBudgetLedgerEntry {
    std::uint32_t id{0};
    std::uint32_t policy_id{0};
    std::string run_uuid;
    std::uint32_t vault_id{0};
    std::optional<std::uint32_t> gateway_credential_id;
    std::optional<std::string> request_uuid;
    std::optional<std::string> operation;
    std::optional<std::string> object_key;
    std::optional<std::string> estimated_cost;
    std::string provider_key;
    std::string currency{"USD"};
    PriceBudgetWindow window{PriceBudgetWindow::PerRun};
    std::string window_start;
    std::string window_end;
    std::string reserved_cost;
    std::optional<std::string> committed_cost;
    std::string status;
    std::string created_at;
};

struct PriceBudgetNotification {
    std::uint32_t id{0};
    std::string type;
    std::string severity{"info"};
    std::string title;
    std::string message;
    std::optional<std::string> scope;
    std::optional<std::uint32_t> vault_id;
    std::optional<std::string> provider_key;
    std::optional<std::uint32_t> policy_id;
    std::optional<std::string> run_uuid;
    nlohmann::json metadata;
    std::optional<std::string> acknowledged_at;
    std::optional<std::uint32_t> acknowledged_by;
    std::string created_at;
    std::optional<std::string> expires_at;
};

struct PriceBudgetOverride {
    std::uint32_t id{0};
    std::optional<std::string> run_uuid;
    std::uint32_t vault_id{0};
    std::optional<std::uint32_t> requested_by;
    std::optional<std::uint32_t> approved_by;
    std::string status{"requested"};
    std::optional<std::string> reason;
    std::string scope{"single_run"};
    std::vector<std::uint32_t> policy_ids;
    std::optional<std::string> estimated_cost;
    std::string currency{"USD"};
    std::string expires_at;
    std::string created_at;
    std::optional<std::string> decided_at;
    std::optional<std::string> used_at;
};

struct PriceBudgetOverrideRequest {
    std::optional<std::string> run_uuid;
    std::uint32_t vault_id{0};
    std::uint32_t requested_by{0};
    std::optional<std::string> reason;
    std::vector<std::uint32_t> policy_ids;
    std::optional<std::string> estimated_cost;
    std::string currency{"USD"};
    std::uint32_t ttl_minutes{30};
};

struct PriceBudgetTrendStats {
    std::string scope;
    std::optional<std::string> provider_key;
    std::optional<std::uint32_t> vault_id;
    std::optional<std::uint32_t> gateway_credential_id;
    std::uint32_t policy_id{0};
    std::string currency{"USD"};
    std::string window_type;
    std::string window_start;
    std::string window_end;
    std::string committed_cost{"0.00000000"};
    std::string reserved_cost{"0.00000000"};
    std::string total_cost{"0.00000000"};
    std::optional<std::string> limit;
    std::optional<std::string> remaining;
    double percent_used{0.0};
    std::optional<std::string> projected_window_cost;
    std::optional<std::string> projected_overage;
    std::optional<std::string> predicted_exhaustion_at;
    std::string confidence{"low"};
    std::string recent_daily_average{"0.00000000"};
    std::string recent_7d_average{"0.00000000"};
    std::string recent_30d_average{"0.00000000"};
    std::vector<std::string> warnings;
};

struct PriceBudgetDashboardStats {
    std::uint32_t active_policies{0};
    std::uint32_t blocked_syncs_24h{0};
    std::uint32_t warning_notifications{0};
    std::uint32_t critical_notifications{0};
    std::uint32_t unacknowledged_notifications{0};
    std::uint32_t pending_overrides{0};
    std::string current_monthly_spend{"0.00000000"};
    std::string projected_monthly_spend{"0.00000000"};
    std::string currency{"USD"};
    std::vector<PriceBudgetTrendStats> trends;
    std::vector<PriceBudgetNotification> active_notifications;
    std::vector<PriceBudgetOverride> recent_overrides;
};

[[nodiscard]] std::string toString(PriceBudgetMode mode);
[[nodiscard]] std::string toString(PriceBudgetScope scope);
[[nodiscard]] std::string toString(PriceBudgetWindow window);
[[nodiscard]] PriceBudgetMode priceBudgetModeFromString(std::string_view value);
[[nodiscard]] PriceBudgetScope priceBudgetScopeFromString(std::string_view value);
[[nodiscard]] PriceBudgetWindow priceBudgetWindowFromString(std::string_view value);
[[nodiscard]] bool isSupportedPriceBudgetProvider(std::string_view providerKey);
[[nodiscard]] bool isValidPriceBudgetDecimal(std::string_view value);
[[nodiscard]] bool isValidPriceBudgetCurrency(std::string_view value);
[[nodiscard]] std::string normalizePriceBudgetCurrency(std::string value);
[[nodiscard]] std::string formatPriceBudgetDecisionForDryRun(const PriceBudgetDecision& decision);

class PriceBudgetService {
public:
    [[nodiscard]] PriceBudgetDecision preflight(const PriceBudgetPreflightRequest& request) const;
    [[nodiscard]] bool isLimitOverrideEligible(const PriceBudgetDecision& decision) const;
    [[nodiscard]] std::vector<std::uint32_t> exceededEnforcePolicyIds(const PriceBudgetDecision& decision) const;
    [[nodiscard]] std::optional<PriceBudgetOverride> consumeApprovedOverride(
        std::uint32_t vaultId,
        const std::vector<std::uint32_t>& policyIds,
        const std::optional<std::string>& runUuid = std::nullopt) const;
    void recordPreflightNotifications(
        const PriceBudgetPreflightRequest& request,
        const PriceBudgetDecision& decision) const;
    void commit(const std::vector<PriceBudgetReservation>& reservations, const std::optional<std::string>& finalCost) const;
    void release(const std::vector<PriceBudgetReservation>& reservations) const;
    void expireStaleReservations() const;
    void expireOverrides() const;

    [[nodiscard]] std::vector<PriceBudgetPolicy> listPolicies(bool includeInactive = false) const;
    [[nodiscard]] PriceBudgetPolicy upsertPolicy(PriceBudgetPolicy policy) const;
    bool disablePolicy(
        PriceBudgetScope scope,
        const std::optional<std::string>& providerKey,
        const std::optional<std::uint32_t>& vaultId,
        const std::optional<std::uint32_t>& gatewayCredentialId = std::nullopt) const;
    [[nodiscard]] std::vector<PriceBudgetLedgerEntry> listLedger(
        std::uint32_t limit = 50,
        const std::optional<std::uint32_t>& vaultId = std::nullopt,
        const std::optional<std::uint32_t>& gatewayCredentialId = std::nullopt) const;

    [[nodiscard]] PriceBudgetNotification createNotification(PriceBudgetNotification notification) const;
    [[nodiscard]] std::vector<PriceBudgetNotification> listNotifications(
        std::uint32_t limit = 50,
        const std::optional<std::uint32_t>& vaultId = std::nullopt,
        bool includeAcknowledged = false) const;
    [[nodiscard]] PriceBudgetNotification acknowledgeNotification(
        std::uint32_t notificationId,
        std::uint32_t userId) const;

    [[nodiscard]] PriceBudgetOverride requestOverride(const PriceBudgetOverrideRequest& request) const;
    [[nodiscard]] PriceBudgetOverride approveOverride(std::uint32_t overrideId, std::uint32_t approvedBy) const;
    [[nodiscard]] PriceBudgetOverride denyOverride(std::uint32_t overrideId, std::uint32_t deniedBy, std::optional<std::string> reason) const;
    [[nodiscard]] std::vector<PriceBudgetOverride> listOverrides(
        std::uint32_t limit = 50,
        const std::optional<std::uint32_t>& vaultId = std::nullopt,
        bool includeExpired = false) const;

    [[nodiscard]] std::vector<PriceBudgetTrendStats> trendStats(
        const std::optional<std::uint32_t>& vaultId = std::nullopt,
        const std::optional<std::uint32_t>& gatewayCredentialId = std::nullopt) const;
    [[nodiscard]] PriceBudgetDashboardStats dashboardStats(const std::optional<std::uint32_t>& vaultId = std::nullopt) const;
};

void to_json(nlohmann::json& j, const PriceBudgetPolicy& policy);
void to_json(nlohmann::json& j, const PriceBudgetWindowCheck& check);
void to_json(nlohmann::json& j, const PriceBudgetDecision& decision);
void to_json(nlohmann::json& j, const PriceBudgetLedgerEntry& entry);
void to_json(nlohmann::json& j, const PriceBudgetNotification& notification);
void to_json(nlohmann::json& j, const PriceBudgetOverride& budgetOverride);
void to_json(nlohmann::json& j, const PriceBudgetTrendStats& stats);
void to_json(nlohmann::json& j, const PriceBudgetDashboardStats& stats);

} // namespace vh::storage::s3::pricing
