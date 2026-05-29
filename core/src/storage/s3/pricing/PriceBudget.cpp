#include "storage/s3/pricing/PriceBudget.hpp"

#include "db/Transactions.hpp"

#include <algorithm>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <cctype>
#include <iomanip>
#include <pqxx/pqxx>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace vh::storage::s3::pricing {
namespace {

using Decimal = boost::multiprecision::cpp_dec_float_50;

constexpr const char* kBudgetMode = "budget_conservative";
constexpr const char* kBudgetFreeTierPolicy = "ignore_account_wide_free_tiers";
constexpr const char* kLedgerReserved = "reserved";
constexpr const char* kLedgerCommitted = "committed";
constexpr const char* kStaleReservationAge = "24 hours";

std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string optionalSql(pqxx::work& txn, const std::optional<std::string>& value) {
    return value && !value->empty() ? txn.quote(*value) : std::string{"NULL"};
}

std::string optionalUintSql(const std::optional<std::uint32_t>& value) {
    return value ? std::to_string(*value) : std::string{"NULL"};
}

std::string optionalInt64Sql(const std::optional<std::int64_t>& value) {
    return value ? std::to_string(*value) : std::string{"NULL"};
}

std::string optionalNumericSql(pqxx::work& txn, const std::optional<std::string>& value) {
    return value && !value->empty() ? txn.quote(*value) + "::numeric" : std::string{"NULL"};
}

std::optional<std::string> optionalString(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return field.as<std::string>();
}

std::optional<std::uint32_t> optionalUInt(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return field.as<std::uint32_t>();
}

std::optional<std::int64_t> optionalInt64(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return std::nullopt;
    return field.as<std::int64_t>();
}

Decimal budgetDecimalFromString(const std::string& value) {
    return Decimal(value.empty() ? "0" : value);
}

std::string budgetFormatDecimal(const Decimal& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << (value == Decimal("0") ? Decimal("0") : value);
    auto text = out.str();
    if (text == "-0.00000000") return "0.00000000";
    return text;
}

PriceBudgetPolicy policyFromRow(const pqxx::row& row) {
    PriceBudgetPolicy policy;
    policy.id = row["id"].as<std::uint32_t>();
    policy.scope = priceBudgetScopeFromString(row["scope"].as<std::string>());
    policy.provider_key = optionalString(row, "provider_key");
    policy.vault_id = optionalUInt(row, "vault_id");
    policy.mode = priceBudgetModeFromString(row["mode"].as<std::string>());
    policy.currency = row["currency"].as<std::string>();
    policy.max_run_cost = optionalString(row, "max_run_cost");
    policy.max_daily_cost = optionalString(row, "max_daily_cost");
    policy.max_monthly_cost = optionalString(row, "max_monthly_cost");
    policy.require_verified_catalog = row["require_verified_catalog"].as<bool>();
    policy.allow_stale_catalog = row["allow_stale_catalog"].as<bool>();
    policy.max_catalog_age_seconds = optionalInt64(row, "max_catalog_age_seconds");
    policy.is_active = row["is_active"].as<bool>();
    return policy;
}

PriceBudgetLedgerEntry ledgerFromRow(const pqxx::row& row) {
    PriceBudgetLedgerEntry entry;
    entry.id = row["id"].as<std::uint32_t>();
    entry.policy_id = row["policy_id"].as<std::uint32_t>();
    entry.run_uuid = row["run_uuid"].as<std::string>();
    entry.vault_id = row["vault_id"].as<std::uint32_t>();
    entry.provider_key = row["provider_key"].as<std::string>();
    entry.currency = row["currency"].as<std::string>();
    entry.window = priceBudgetWindowFromString(row["window_type"].as<std::string>());
    entry.window_start = row["window_start"].as<std::string>();
    entry.window_end = row["window_end"].as<std::string>();
    entry.reserved_cost = row["reserved_cost"].as<std::string>();
    entry.committed_cost = optionalString(row, "committed_cost");
    entry.status = row["status"].as<std::string>();
    entry.created_at = row["created_at"].as<std::string>();
    return entry;
}

std::optional<std::string> limitForWindow(const PriceBudgetPolicy& policy, const PriceBudgetWindow window) {
    switch (window) {
    case PriceBudgetWindow::PerRun:
        return policy.max_run_cost;
    case PriceBudgetWindow::Daily:
        return policy.max_daily_cost;
    case PriceBudgetWindow::Monthly:
        return policy.max_monthly_cost;
    }
    return std::nullopt;
}

std::vector<PriceBudgetWindow> configuredWindows(const PriceBudgetPolicy& policy) {
    std::vector<PriceBudgetWindow> windows;
    if (policy.max_run_cost) windows.push_back(PriceBudgetWindow::PerRun);
    if (policy.max_daily_cost) windows.push_back(PriceBudgetWindow::Daily);
    if (policy.max_monthly_cost) windows.push_back(PriceBudgetWindow::Monthly);
    if (windows.empty()) windows.push_back(PriceBudgetWindow::PerRun);
    return windows;
}

std::string windowStartExpr(const PriceBudgetWindow window) {
    switch (window) {
    case PriceBudgetWindow::PerRun:
        return "CURRENT_TIMESTAMP";
    case PriceBudgetWindow::Daily:
        return "date_trunc('day', CURRENT_TIMESTAMP)";
    case PriceBudgetWindow::Monthly:
        return "date_trunc('month', CURRENT_TIMESTAMP)";
    }
    return "CURRENT_TIMESTAMP";
}

std::string windowEndExpr(const PriceBudgetWindow window) {
    switch (window) {
    case PriceBudgetWindow::PerRun:
        return "CURRENT_TIMESTAMP + interval '1 second'";
    case PriceBudgetWindow::Daily:
        return "date_trunc('day', CURRENT_TIMESTAMP) + interval '1 day'";
    case PriceBudgetWindow::Monthly:
        return "date_trunc('month', CURRENT_TIMESTAMP) + interval '1 month'";
    }
    return "CURRENT_TIMESTAMP + interval '1 second'";
}

std::string activeLedgerCostSql() {
    return "COALESCE(committed_cost, reserved_cost)";
}

std::string usedForWindow(pqxx::work& txn, const PriceBudgetPolicy& policy, const PriceBudgetWindow window) {
    if (window == PriceBudgetWindow::PerRun) return "0.00000000";

    const auto sql =
        "SELECT COALESCE(SUM(" + activeLedgerCostSql() + "), 0)::numeric(20,8)::text AS used "
        "FROM s3_price_budget_ledger "
        "WHERE policy_id = " + std::to_string(policy.id) + " "
        "AND window_type = " + txn.quote(toString(window)) + " "
        "AND window_start = " + windowStartExpr(window) + " "
        "AND status IN (" + txn.quote(kLedgerReserved) + ", " + txn.quote(kLedgerCommitted) + ")";
    const auto result = txn.exec(sql);
    if (result.empty()) return "0.00000000";
    return result.one_row()["used"].as<std::string>();
}

std::string policyWhereClause(
    pqxx::work& txn,
    const std::uint32_t vaultId,
    const std::string& providerKey,
    const bool providerSupported,
    const bool includeInactive) {
    std::string where = includeInactive ? "TRUE" : "is_active = TRUE AND mode <> 'off'";
    where += " AND (";
    if (providerSupported)
        where += "scope = 'global' OR ";
    where += "(scope = 'provider' AND provider_key = " + txn.quote(providerKey) + ") OR ";
    where += "(scope = 'vault' AND vault_id = " + std::to_string(vaultId) +
        " AND (provider_key IS NULL OR provider_key = " + txn.quote(providerKey) + "))";
    where += ")";
    return where;
}

std::string policyIdentityWhere(
    pqxx::work& txn,
    const PriceBudgetScope scope,
    const std::optional<std::string>& providerKey,
    const std::optional<std::uint32_t>& vaultId) {
    switch (scope) {
    case PriceBudgetScope::Global:
        return "scope = 'global'";
    case PriceBudgetScope::Provider:
        if (!providerKey) throw std::invalid_argument("provider budget requires provider key");
        return "scope = 'provider' AND provider_key = " + txn.quote(*providerKey);
    case PriceBudgetScope::Vault:
        if (!vaultId) throw std::invalid_argument("vault budget requires vault id");
        return "scope = 'vault' AND vault_id = " + std::to_string(*vaultId) +
            " AND " + (providerKey
                ? "provider_key = " + txn.quote(*providerKey)
                : std::string{"provider_key IS NULL"});
    }
    return "FALSE";
}

std::string scopeLabel(const PriceBudgetPolicy& policy) {
    auto label = toString(policy.scope);
    if (policy.provider_key) label += ":" + *policy.provider_key;
    if (policy.vault_id) label += ":" + std::to_string(*policy.vault_id);
    return label;
}

void appendWarning(PriceBudgetDecision& decision, const PriceBudgetPolicy& policy, std::string warning) {
    decision.warnings.push_back("policy " + std::to_string(policy.id) + " (" + scopeLabel(policy) + "): " + std::move(warning));
}

void setBlocked(
    PriceBudgetDecision& decision,
    const PriceBudgetPolicy& policy,
    std::string reason,
    const std::optional<PriceBudgetWindowCheck>& check = std::nullopt) {
    if (decision.stalled) return;
    decision.allowed = false;
    decision.stalled = true;
    decision.exceeded_policy_id = policy.id;
    decision.exceeded_scope = scopeLabel(policy);
    decision.currency = policy.currency;
    decision.reason = std::move(reason);
    if (check) {
        decision.limit = check->limit.value_or("");
        decision.remaining_before = check->remaining_before;
        decision.requested = check->requested;
    }
}

void validatePolicyForWrite(const PriceBudgetPolicy& policy) {
    if (!isValidPriceBudgetCurrency(policy.currency))
        throw std::invalid_argument("invalid currency: " + policy.currency);

    for (const auto& value : {policy.max_run_cost, policy.max_daily_cost, policy.max_monthly_cost}) {
        if (value && !isValidPriceBudgetDecimal(*value))
            throw std::invalid_argument("invalid budget decimal: " + *value);
    }

    if (policy.scope == PriceBudgetScope::Global && (policy.provider_key || policy.vault_id))
        throw std::invalid_argument("global price budget cannot include provider_key or vault_id");
    if (policy.scope == PriceBudgetScope::Provider && (!policy.provider_key || policy.vault_id))
        throw std::invalid_argument("provider price budget requires provider_key and no vault_id");
    if (policy.scope == PriceBudgetScope::Vault && !policy.vault_id)
        throw std::invalid_argument("vault price budget requires vault_id");
}

std::vector<std::uint32_t> reservationIds(const std::vector<PriceBudgetReservation>& reservations) {
    std::vector<std::uint32_t> ids;
    ids.reserve(reservations.size());
    for (const auto& reservation : reservations)
        if (reservation.id != 0) ids.push_back(reservation.id);
    return ids;
}

std::string idsSql(const std::vector<std::uint32_t>& ids) {
    std::ostringstream out;
    out << "(";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) out << ",";
        out << ids[i];
    }
    out << ")";
    return out.str();
}

bool estimateCanBeReserved(const PriceEstimateReport& estimate) {
    return estimate.available &&
        estimate.estimate_mode == kBudgetMode &&
        estimate.free_tier_policy == kBudgetFreeTierPolicy &&
        !estimate.estimated_cost.empty() &&
        !estimate.currency.empty();
}

} // namespace

std::string toString(const PriceBudgetMode mode) {
    switch (mode) {
    case PriceBudgetMode::Off:
        return "off";
    case PriceBudgetMode::Report:
        return "report";
    case PriceBudgetMode::Warn:
        return "warn";
    case PriceBudgetMode::Enforce:
        return "enforce";
    }
    return "off";
}

std::string toString(const PriceBudgetScope scope) {
    switch (scope) {
    case PriceBudgetScope::Global:
        return "global";
    case PriceBudgetScope::Provider:
        return "provider";
    case PriceBudgetScope::Vault:
        return "vault";
    }
    return "vault";
}

std::string toString(const PriceBudgetWindow window) {
    switch (window) {
    case PriceBudgetWindow::PerRun:
        return "per_run";
    case PriceBudgetWindow::Daily:
        return "daily";
    case PriceBudgetWindow::Monthly:
        return "monthly";
    }
    return "per_run";
}

PriceBudgetMode priceBudgetModeFromString(const std::string_view value) {
    const auto normalized = lower(std::string(value));
    if (normalized == "off") return PriceBudgetMode::Off;
    if (normalized == "report") return PriceBudgetMode::Report;
    if (normalized == "warn" || normalized == "warning") return PriceBudgetMode::Warn;
    if (normalized == "enforce" || normalized == "enforced") return PriceBudgetMode::Enforce;
    throw std::invalid_argument("unknown price budget mode: " + normalized);
}

PriceBudgetScope priceBudgetScopeFromString(const std::string_view value) {
    const auto normalized = lower(std::string(value));
    if (normalized == "global") return PriceBudgetScope::Global;
    if (normalized == "provider") return PriceBudgetScope::Provider;
    if (normalized == "vault") return PriceBudgetScope::Vault;
    throw std::invalid_argument("unknown price budget scope: " + normalized);
}

PriceBudgetWindow priceBudgetWindowFromString(const std::string_view value) {
    const auto normalized = lower(std::string(value));
    if (normalized == "per_run" || normalized == "run") return PriceBudgetWindow::PerRun;
    if (normalized == "daily" || normalized == "day") return PriceBudgetWindow::Daily;
    if (normalized == "monthly" || normalized == "month") return PriceBudgetWindow::Monthly;
    throw std::invalid_argument("unknown price budget window: " + normalized);
}

bool isSupportedPriceBudgetProvider(const std::string_view providerKey) {
    static const std::unordered_set<std::string> supported{"aws-s3", "cloudflare-r2"};
    return supported.contains(std::string(providerKey));
}

bool isValidPriceBudgetDecimal(const std::string_view value) {
    const auto text = trim(std::string(value));
    if (text.empty() || text.front() == '-') return false;

    bool dot = false;
    bool digit = false;
    std::size_t fractional = 0;
    for (const char ch : text) {
        if (ch >= '0' && ch <= '9') {
            digit = true;
            if (dot) ++fractional;
            continue;
        }
        if (ch == '.' && !dot) {
            dot = true;
            continue;
        }
        return false;
    }

    if (!digit || fractional > 8) return false;
    try {
        return budgetDecimalFromString(text) >= Decimal("0");
    } catch (...) {
        return false;
    }
}

bool isValidPriceBudgetCurrency(const std::string_view value) {
    if (value.size() < 3 || value.size() > 8) return false;
    return std::ranges::all_of(value, [](const unsigned char c) {
        return std::isalnum(c) != 0;
    });
}

std::string normalizePriceBudgetCurrency(std::string value) {
    value = trim(std::move(value));
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string formatPriceBudgetDecisionForDryRun(const PriceBudgetDecision& decision) {
    std::ostringstream out;
    out << "  Price budget decision:\n"
        << "    Applicable policies: " << decision.policies.size() << "\n"
        << "    Result: " << (decision.allowed ? (decision.warnings.empty() ? "pass" : "warn") : "fail") << "\n";

    if (!decision.reason.empty())
        out << "    Reason: " << decision.reason << "\n";
    if (!decision.warnings.empty()) {
        out << "    Warnings:\n";
        for (const auto& warning : decision.warnings) out << "      - " << warning << "\n";
    }
    if (!decision.checks.empty()) {
        out << "    Budget windows:\n";
        for (const auto& check : decision.checks) {
            out << "      - policy " << check.policy_id
                << " " << toString(check.scope)
                << " " << toString(check.window)
                << ": requested " << check.requested << " " << check.currency;
            if (check.limit) {
                out << ", remaining before " << check.remaining_before
                    << ", limit " << *check.limit
                    << (check.exceeded ? " (exceeded)" : "");
            } else {
                out << ", no limit";
            }
            out << "\n";
        }
    }
    out << "    Enforcement would stall: " << (decision.stalled ? "yes" : "no");
    return out.str();
}

PriceBudgetDecision PriceBudgetService::preflight(const PriceBudgetPreflightRequest& request) const {
    return db::Transactions::exec("PriceBudgetService::preflight", [&](pqxx::work& txn) {
        PriceBudgetDecision decision;

        txn.exec(
            "UPDATE s3_price_budget_ledger "
            "SET status = 'expired', updated_at = CURRENT_TIMESTAMP "
            "WHERE status = 'reserved' "
            "AND created_at < CURRENT_TIMESTAMP - interval '" + std::string(kStaleReservationAge) + "'");

        const auto policySql =
            "SELECT * FROM s3_price_budget_policy WHERE " +
            policyWhereClause(txn, request.vault_id, request.provider_key, request.provider_supported, false) +
            " ORDER BY id FOR UPDATE";
        const auto policies = txn.exec(policySql);
        for (const auto& row : policies) decision.policies.push_back(policyFromRow(row));
        if (decision.policies.empty()) return decision;

        const bool canReserve = estimateCanBeReserved(request.estimate);
        for (const auto& policy : decision.policies) {
            const auto mode = policy.mode;
            if (mode == PriceBudgetMode::Off) continue;

            auto policyIssue = std::optional<std::string>{};
            if (!request.provider_supported && policy.scope != PriceBudgetScope::Global)
                policyIssue = "price budget cannot be evaluated: provider is unsupported for local S3 pricing";
            else if (!request.estimate.supported)
                policyIssue = "price budget cannot be evaluated: " + request.estimate.unavailable_reason;
            else if (!request.estimate.available)
                policyIssue = "price budget cannot be evaluated: " + request.estimate.unavailable_reason;
            else if (request.estimate.estimate_mode != kBudgetMode ||
                     request.estimate.free_tier_policy != kBudgetFreeTierPolicy)
                policyIssue = "price budget cannot be evaluated from a non-budget-conservative estimate";
            else if (policy.require_verified_catalog && !request.estimate.catalog_verified)
                policyIssue = "price budget cannot be evaluated: verified pricing catalog is required";
            else if (request.estimate.stale && !policy.allow_stale_catalog)
                policyIssue = "price budget cannot be evaluated: pricing catalog is stale";
            else if (request.estimate.stale && policy.allow_stale_catalog &&
                     policy.max_catalog_age_seconds && request.estimate.catalog_age_seconds &&
                     *request.estimate.catalog_age_seconds > *policy.max_catalog_age_seconds)
                policyIssue = "price budget cannot be evaluated: stale pricing catalog is older than policy max age";
            else if (normalizePriceBudgetCurrency(request.estimate.currency) != normalizePriceBudgetCurrency(policy.currency))
                policyIssue = "price budget currency mismatch: estimate is " + request.estimate.currency +
                    ", policy is " + policy.currency;

            if (policyIssue) {
                if (mode == PriceBudgetMode::Enforce) setBlocked(decision, policy, *policyIssue);
                else appendWarning(decision, policy, *policyIssue);
                continue;
            }

            if (!canReserve) {
                if (mode == PriceBudgetMode::Enforce)
                    setBlocked(decision, policy, "price budget cannot be evaluated");
                else appendWarning(decision, policy, "price budget cannot be evaluated");
                continue;
            }

            const auto requested = budgetDecimalFromString(request.estimate.estimated_cost);
            for (const auto window : configuredWindows(policy)) {
                PriceBudgetWindowCheck check;
                check.policy_id = policy.id;
                check.scope = policy.scope;
                check.mode = mode;
                check.window = window;
                check.currency = policy.currency;
                check.limit = limitForWindow(policy, window);
                check.used_before = usedForWindow(txn, policy, window);
                check.requested = budgetFormatDecimal(requested);

                if (check.limit) {
                    const auto limit = budgetDecimalFromString(*check.limit);
                    const auto used = budgetDecimalFromString(check.used_before);
                    check.remaining_before = budgetFormatDecimal(limit - used);
                    check.exceeded = used + requested > limit;
                } else {
                    check.remaining_before = "";
                }

                decision.checks.push_back(check);
                if (!check.exceeded) continue;

                if (mode == PriceBudgetMode::Enforce) {
                    setBlocked(
                        decision,
                        policy,
                        "S3 price budget exceeded for " + toString(window) +
                            " " + scopeLabel(policy),
                        check);
                } else if (mode == PriceBudgetMode::Warn) {
                    appendWarning(
                        decision,
                        policy,
                        "S3 price budget would exceed " + toString(window) +
                            " limit " + *check.limit + " " + policy.currency +
                            " with request " + check.requested);
                }
            }
        }

        if (decision.stalled || request.dry_run || !canReserve) return decision;

        for (const auto& policy : decision.policies) {
            if (policy.mode == PriceBudgetMode::Off) continue;
            if (normalizePriceBudgetCurrency(request.estimate.currency) != normalizePriceBudgetCurrency(policy.currency))
                continue;
            for (const auto window : configuredWindows(policy)) {
                const auto insertSql =
                    "INSERT INTO s3_price_budget_ledger "
                    "(policy_id, run_uuid, vault_id, provider_key, currency, window_type, window_start, window_end, reserved_cost, status) "
                    "VALUES (" +
                    std::to_string(policy.id) + ", " +
                    txn.quote(request.run_uuid) + ", " +
                    std::to_string(request.vault_id) + ", " +
                    txn.quote(request.provider_key) + ", " +
                    txn.quote(policy.currency) + ", " +
                    txn.quote(toString(window)) + ", " +
                    windowStartExpr(window) + ", " +
                    windowEndExpr(window) + ", " +
                    txn.quote(request.estimate.estimated_cost) + "::numeric, 'reserved') "
                    "RETURNING id";
                const auto inserted = txn.exec(insertSql);
                if (inserted.empty()) throw std::runtime_error("failed to create S3 price budget reservation");
                decision.reservations.push_back({
                    .id = inserted.one_row()["id"].as<std::uint32_t>(),
                    .policy_id = policy.id,
                    .window = window
                });
            }
        }

        return decision;
    });
}

void PriceBudgetService::commit(
    const std::vector<PriceBudgetReservation>& reservations,
    const std::optional<std::string>& finalCost) const {
    const auto ids = reservationIds(reservations);
    if (ids.empty()) return;
    if (finalCost && !isValidPriceBudgetDecimal(*finalCost))
        throw std::invalid_argument("invalid committed S3 price budget decimal: " + *finalCost);

    db::Transactions::exec("PriceBudgetService::commit", [&](pqxx::work& txn) {
        const auto committed = finalCost
            ? txn.quote(*finalCost) + "::numeric"
            : std::string{"reserved_cost"};
        txn.exec(
            "UPDATE s3_price_budget_ledger "
            "SET status = 'committed', committed_cost = " + committed + ", updated_at = CURRENT_TIMESTAMP "
            "WHERE id IN " + idsSql(ids) + " AND status = 'reserved'");
    });
}

void PriceBudgetService::release(const std::vector<PriceBudgetReservation>& reservations) const {
    const auto ids = reservationIds(reservations);
    if (ids.empty()) return;

    db::Transactions::exec("PriceBudgetService::release", [&](pqxx::work& txn) {
        txn.exec(
            "UPDATE s3_price_budget_ledger "
            "SET status = 'released', updated_at = CURRENT_TIMESTAMP "
            "WHERE id IN " + idsSql(ids) + " AND status = 'reserved'");
    });
}

void PriceBudgetService::expireStaleReservations() const {
    db::Transactions::exec("PriceBudgetService::expireStaleReservations", [](pqxx::work& txn) {
        txn.exec(
            "UPDATE s3_price_budget_ledger "
            "SET status = 'expired', updated_at = CURRENT_TIMESTAMP "
            "WHERE status = 'reserved' "
            "AND created_at < CURRENT_TIMESTAMP - interval '" + std::string(kStaleReservationAge) + "'");
    });
}

std::vector<PriceBudgetPolicy> PriceBudgetService::listPolicies(const bool includeInactive) const {
    return db::Transactions::exec("PriceBudgetService::listPolicies", [&](pqxx::work& txn) {
        const auto sql = std::string("SELECT * FROM s3_price_budget_policy ") +
            (includeInactive ? "" : "WHERE is_active = TRUE ") +
            "ORDER BY scope, provider_key NULLS FIRST, vault_id NULLS FIRST, id";
        const auto result = txn.exec(sql);
        std::vector<PriceBudgetPolicy> policies;
        policies.reserve(result.size());
        for (const auto& row : result) policies.push_back(policyFromRow(row));
        return policies;
    });
}

PriceBudgetPolicy PriceBudgetService::upsertPolicy(PriceBudgetPolicy policy) const {
    validatePolicyForWrite(policy);
    policy.currency = normalizePriceBudgetCurrency(policy.currency);

    return db::Transactions::exec("PriceBudgetService::upsertPolicy", [&](pqxx::work& txn) {
        const auto identity = policyIdentityWhere(txn, policy.scope, policy.provider_key, policy.vault_id);
        const auto existing = txn.exec("SELECT id FROM s3_price_budget_policy WHERE " + identity + " LIMIT 1");

        std::string sql;
        if (existing.empty()) {
            sql =
                "INSERT INTO s3_price_budget_policy "
                "(scope, provider_key, vault_id, mode, currency, max_run_cost, max_daily_cost, max_monthly_cost, "
                "require_verified_catalog, allow_stale_catalog, max_catalog_age_seconds, is_active) VALUES (" +
                txn.quote(toString(policy.scope)) + ", " +
                optionalSql(txn, policy.provider_key) + ", " +
                optionalUintSql(policy.vault_id) + ", " +
                txn.quote(toString(policy.mode)) + ", " +
                txn.quote(policy.currency) + ", " +
                optionalNumericSql(txn, policy.max_run_cost) + ", " +
                optionalNumericSql(txn, policy.max_daily_cost) + ", " +
                optionalNumericSql(txn, policy.max_monthly_cost) + ", " +
                std::string(policy.require_verified_catalog ? "TRUE" : "FALSE") + ", " +
                std::string(policy.allow_stale_catalog ? "TRUE" : "FALSE") + ", " +
                optionalInt64Sql(policy.max_catalog_age_seconds) + ", TRUE) RETURNING *";
        } else {
            sql =
                "UPDATE s3_price_budget_policy SET "
                "mode = " + txn.quote(toString(policy.mode)) + ", "
                "currency = " + txn.quote(policy.currency) + ", "
                "max_run_cost = " + optionalNumericSql(txn, policy.max_run_cost) + ", "
                "max_daily_cost = " + optionalNumericSql(txn, policy.max_daily_cost) + ", "
                "max_monthly_cost = " + optionalNumericSql(txn, policy.max_monthly_cost) + ", "
                "require_verified_catalog = " + std::string(policy.require_verified_catalog ? "TRUE" : "FALSE") + ", "
                "allow_stale_catalog = " + std::string(policy.allow_stale_catalog ? "TRUE" : "FALSE") + ", "
                "max_catalog_age_seconds = " + optionalInt64Sql(policy.max_catalog_age_seconds) + ", "
                "is_active = TRUE "
                "WHERE id = " + std::to_string(existing.one_row()["id"].as<std::uint32_t>()) + " RETURNING *";
        }

        const auto result = txn.exec(sql);
        if (result.empty()) throw std::runtime_error("failed to upsert S3 price budget policy");
        return policyFromRow(result.one_row());
    });
}

bool PriceBudgetService::disablePolicy(
    const PriceBudgetScope scope,
    const std::optional<std::string>& providerKey,
    const std::optional<std::uint32_t>& vaultId) const {
    return db::Transactions::exec("PriceBudgetService::disablePolicy", [&](pqxx::work& txn) {
        const auto identity = policyIdentityWhere(txn, scope, providerKey, vaultId);
        const auto result = txn.exec(
            "UPDATE s3_price_budget_policy "
            "SET is_active = FALSE, mode = 'off' "
            "WHERE " + identity + " RETURNING id");
        return !result.empty();
    });
}

std::vector<PriceBudgetLedgerEntry> PriceBudgetService::listLedger(const std::uint32_t limit) const {
    return db::Transactions::exec("PriceBudgetService::listLedger", [&](pqxx::work& txn) {
        const auto safeLimit = std::max<std::uint32_t>(1, std::min<std::uint32_t>(limit, 1000));
        const auto result = txn.exec(
            "SELECT * FROM s3_price_budget_ledger "
            "ORDER BY created_at DESC, id DESC LIMIT " + std::to_string(safeLimit));
        std::vector<PriceBudgetLedgerEntry> entries;
        entries.reserve(result.size());
        for (const auto& row : result) entries.push_back(ledgerFromRow(row));
        return entries;
    });
}

} // namespace vh::storage::s3::pricing
