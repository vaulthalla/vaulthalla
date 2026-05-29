#pragma once

#include "storage/s3/pricing/PriceBotModels.hpp"

#include <cstdint>
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
    Vault
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
};

struct PriceBudgetLedgerEntry {
    std::uint32_t id{0};
    std::uint32_t policy_id{0};
    std::string run_uuid;
    std::uint32_t vault_id{0};
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
    void commit(const std::vector<PriceBudgetReservation>& reservations, const std::optional<std::string>& finalCost) const;
    void release(const std::vector<PriceBudgetReservation>& reservations) const;
    void expireStaleReservations() const;

    [[nodiscard]] std::vector<PriceBudgetPolicy> listPolicies(bool includeInactive = false) const;
    [[nodiscard]] PriceBudgetPolicy upsertPolicy(PriceBudgetPolicy policy) const;
    bool disablePolicy(PriceBudgetScope scope, const std::optional<std::string>& providerKey, const std::optional<std::uint32_t>& vaultId) const;
    [[nodiscard]] std::vector<PriceBudgetLedgerEntry> listLedger(std::uint32_t limit = 50) const;
};

} // namespace vh::storage::s3::pricing
