#pragma once

#include "ops/Actor.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// S3 price budget policies, shared by `vh pricing budget ...` and the ws pricing.budget.* commands.
//  - global and provider policies are super-admin only, and a provider policy names a supported provider;
//  - a vault policy needs vault Edit through the resolver (an owner holds it through the self scope or not at
//    all: the web used to let any owner edit, and so disable, a budget an admin had imposed), and only S3
//    vaults have one;
//  - gateway credential policies are ops::s3_gateway's (admin.s3_gateway.manage_budgets);
//  - reading: super admins see everything; others see policies, ledger rows and notifications for vaults
//    they can view and gateway credentials they own.
namespace vh::ops::pricing {

using Policy = storage::s3::pricing::PriceBudgetPolicy;
using Scope = storage::s3::pricing::PriceBudgetScope;

struct PolicySpec {
    Scope scope{Scope::Vault};
    std::optional<std::string> provider_key{};
    std::optional<uint32_t> vault_id{};
    std::optional<uint32_t> gateway_credential_id{};
    std::optional<std::string> mode{};                  // default report (gateway scopes: enforce)
    std::optional<std::string> currency{};              // default USD
    std::optional<std::string> max_run_cost{};
    std::optional<std::string> max_daily_cost{};
    std::optional<std::string> max_monthly_cost{};
    std::optional<bool> require_verified_catalog{};     // default true
    std::optional<bool> allow_stale_catalog{};          // default false
    std::optional<int64_t> max_catalog_age_seconds{};   // default 43200
};

struct Filter {
    std::optional<uint32_t> vault_id{};
    std::optional<uint32_t> gateway_credential_id{};
    bool include_inactive{true};
};

struct Status {
    std::vector<Policy> policies;
    std::vector<storage::s3::pricing::PriceBudgetLedgerEntry> ledger;
    std::vector<storage::s3::pricing::PriceBudgetTrendStats> trends;
    std::vector<storage::s3::pricing::PriceBudgetNotification> notifications;
    std::vector<storage::s3::pricing::PriceBudgetOverride> overrides;
};

// A provider key as a budget names it (lower case, supported), or Invalid.
[[nodiscard]] std::string providerKey(const std::string& value);

Policy upsertPolicy(const Actor& actor, const PolicySpec& spec);
bool disablePolicy(const Actor& actor, Scope scope, const std::optional<std::string>& providerKey,
                   std::optional<uint32_t> vaultId, std::optional<uint32_t> gatewayCredentialId = std::nullopt);
[[nodiscard]] std::vector<Policy> listPolicies(const Actor& actor, const Filter& filter = {});
[[nodiscard]] std::vector<storage::s3::pricing::PriceBudgetLedgerEntry> ledger(const Actor& actor, const Filter& filter,
                                                                              uint32_t limit);
[[nodiscard]] Status status(const Actor& actor, const Filter& filter, uint32_t limit);

}
