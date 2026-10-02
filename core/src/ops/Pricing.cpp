#include "ops/Pricing.hpp"

#include "ops/S3Gateway.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "rbac/permission/admin/Vaults.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <cctype>

namespace vh::ops::pricing {

namespace {

namespace budget = storage::s3::pricing;
using VaultPerm = rbac::permission::admin::VaultPermissions;

bool isGatewayScope(const Scope scope) {
    return scope == Scope::GatewayCredential || scope == Scope::GatewayCredentialVault;
}

bool ownsVault(const Actor& actor, const uint32_t vaultId) {
    try {
        return db::query::vault::Vault::getVaultOwnerId(vaultId) == actor->id;
    } catch (const std::exception&) {
        return false;
    }
}

bool canViewVaultBudget(const Actor& actor, const uint32_t vaultId) {
    if (actor->isSuperAdmin() || ownsVault(actor, vaultId)) return true;
    return rbac::resolver::Admin::has<VaultPerm>({
        .user = actor, .permissions = {VaultPerm::View, VaultPerm::ViewStats}, .vault_id = vaultId});
}

bool canEditVaultBudget(const Actor& actor, const uint32_t vaultId) {
    return rbac::resolver::Admin::has<VaultPerm>({.user = actor, .permission = VaultPerm::Edit, .vault_id = vaultId});
}

void requireVaultBudgetView(const Actor& actor, const uint32_t vaultId) {
    if (!canViewVaultBudget(actor, vaultId)) throw Denied("you do not have permission to view this vault's price budgets");
}

void requireSuperAdmin(const Actor& actor, const std::string& what) {
    if (!actor->isSuperAdmin()) throw Denied("only super admins may " + what);
}

bool ownsCredential(const Actor& actor, const std::optional<uint32_t>& credentialId) {
    if (!credentialId) return false;
    return std::ranges::any_of(db::query::s3::Gateway::listCredentialsForPrincipal(actor->id),
                               [&](const auto& c) { return c.id == *credentialId; });
}

// A vault policy's vault: it exists, it is S3-backed, and the actor may edit it.
void requireVaultPolicyAuthority(const Actor& actor, const std::optional<uint32_t>& vaultId) {
    if (!vaultId) throw Invalid("a vault price budget needs a vault");
    const auto vault = db::query::vault::Vault::getVault(*vaultId);
    if (!vault) throw NotFound("vault not found: " + std::to_string(*vaultId));
    if (vault->type != vault::model::VaultType::S3) throw Invalid("price budgets are only available for S3 vaults");
    if (!canEditVaultBudget(actor, *vaultId)) throw Denied("you do not have permission to change this vault's price budget");
}

bool policyVisible(const Actor& actor, const Policy& policy, const std::optional<uint32_t>& scopedVaultId) {
    if (actor->isSuperAdmin()) return true;
    if (policy.gateway_credential_id) {
        if (ownsCredential(actor, policy.gateway_credential_id)) return true;
        return policy.vault_id && canViewVaultBudget(actor, *policy.vault_id);
    }
    if (policy.vault_id) return canViewVaultBudget(actor, *policy.vault_id);
    if (scopedVaultId) return canViewVaultBudget(actor, *scopedVaultId);
    return false;
}

ops::s3_gateway::BudgetScope gatewayScope(const Scope scope) {
    return scope == Scope::GatewayCredential ? ops::s3_gateway::BudgetScope::Key : ops::s3_gateway::BudgetScope::KeyVault;
}

}

std::string providerKey(const std::string& value) {
    auto provider = value;
    std::ranges::transform(provider, provider.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!budget::isSupportedPriceBudgetProvider(provider))
        throw Invalid("unsupported provider for price budgets: " + value + " (supported: aws-s3, cloudflare-r2)");
    return provider;
}

Policy upsertPolicy(const Actor& actor, const PolicySpec& spec) {
    requireActor(actor);
    if (isGatewayScope(spec.scope)) {
        if (!spec.gateway_credential_id) throw Invalid("a gateway credential budget needs a credential");
        return ops::s3_gateway::upsertBudget(actor, {
            .scope = gatewayScope(spec.scope), .credential_id = *spec.gateway_credential_id, .vault_id = spec.vault_id,
            .mode = spec.mode, .currency = spec.currency, .max_run_cost = spec.max_run_cost,
            .max_daily_cost = spec.max_daily_cost, .max_monthly_cost = spec.max_monthly_cost,
            .require_verified_catalog = spec.require_verified_catalog, .allow_stale_catalog = spec.allow_stale_catalog,
            .max_catalog_age_seconds = spec.max_catalog_age_seconds});
    }

    Policy policy;
    policy.scope = spec.scope;
    if (spec.scope == Scope::Global) {
        requireSuperAdmin(actor, "change the global price budget");
    } else if (spec.scope == Scope::Provider) {
        requireSuperAdmin(actor, "change provider price budgets");
        if (!spec.provider_key) throw Invalid("a provider price budget needs a provider");
        policy.provider_key = providerKey(*spec.provider_key);
    } else {
        requireVaultPolicyAuthority(actor, spec.vault_id);
        policy.vault_id = spec.vault_id;
    }

    try {
        policy.mode = budget::priceBudgetModeFromString(spec.mode.value_or("report"));
    } catch (const std::exception& e) {
        throw Invalid(e.what());
    }
    policy.currency = budget::normalizePriceBudgetCurrency(spec.currency.value_or("USD"));
    if (!budget::isValidPriceBudgetCurrency(policy.currency)) throw Invalid("currency must be 3-8 alphanumeric characters");
    for (const auto& cost : {spec.max_run_cost, spec.max_daily_cost, spec.max_monthly_cost})
        if (cost && !budget::isValidPriceBudgetDecimal(*cost))
            throw Invalid("budget limits must be non-negative decimals with at most 8 fractional digits: " + *cost);
    policy.max_run_cost = spec.max_run_cost;
    policy.max_daily_cost = spec.max_daily_cost;
    policy.max_monthly_cost = spec.max_monthly_cost;
    policy.require_verified_catalog = spec.require_verified_catalog.value_or(true);
    policy.allow_stale_catalog = spec.allow_stale_catalog.value_or(false);
    policy.max_catalog_age_seconds = spec.max_catalog_age_seconds.value_or(43200);
    if (*policy.max_catalog_age_seconds <= 0) throw Invalid("the catalog age limit must be a positive number of seconds");
    try {
        return budget::PriceBudgetService{}.upsertPolicy(std::move(policy));
    } catch (const std::invalid_argument& e) {
        throw Invalid(e.what());
    }
}

bool disablePolicy(const Actor& actor, const Scope scope, const std::optional<std::string>& provider,
                   const std::optional<uint32_t> vaultId, const std::optional<uint32_t> gatewayCredentialId) {
    requireActor(actor);
    if (isGatewayScope(scope)) {
        if (!gatewayCredentialId) throw Invalid("a gateway credential budget needs a credential");
        return ops::s3_gateway::disableBudget(actor, gatewayScope(scope), *gatewayCredentialId, vaultId);
    }
    std::optional<std::string> key;
    if (scope == Scope::Global) {
        requireSuperAdmin(actor, "disable the global price budget");
    } else if (scope == Scope::Provider) {
        requireSuperAdmin(actor, "disable provider price budgets");
        if (!provider) throw Invalid("a provider price budget needs a provider");
        key = providerKey(*provider);
    } else {
        requireVaultPolicyAuthority(actor, vaultId);
    }
    return budget::PriceBudgetService{}.disablePolicy(scope, key, scope == Scope::Vault ? vaultId : std::nullopt);
}

std::vector<Policy> listPolicies(const Actor& actor, const Filter& filter) {
    requireActor(actor);
    if (filter.vault_id) requireVaultBudgetView(actor, *filter.vault_id);
    auto policies = budget::PriceBudgetService{}.listPolicies(filter.include_inactive);
    std::erase_if(policies, [&](const Policy& policy) {
        if (!policyVisible(actor, policy, filter.vault_id)) return true;
        if (filter.gateway_credential_id) {
            if (!isGatewayScope(policy.scope)) return true;
            if (policy.gateway_credential_id != filter.gateway_credential_id) return true;
            return filter.vault_id && policy.vault_id && *policy.vault_id != *filter.vault_id;
        }
        if (!filter.vault_id) return false;
        if (policy.vault_id && *policy.vault_id != *filter.vault_id) return true;
        return policy.scope == Scope::GatewayCredential;
    });
    return policies;
}

std::vector<budget::PriceBudgetLedgerEntry> ledger(const Actor& actor, const Filter& filter, const uint32_t limit) {
    requireActor(actor);
    if (!actor->isSuperAdmin()) {
        if (ownsCredential(actor, filter.gateway_credential_id)) {
            if (filter.vault_id) requireVaultBudgetView(actor, *filter.vault_id);
        } else {
            if (!filter.vault_id) throw Denied("name a vault you can view to see its price budget ledger");
            requireVaultBudgetView(actor, *filter.vault_id);
        }
    } else if (filter.vault_id) {
        requireVaultBudgetView(actor, *filter.vault_id);
    }
    return budget::PriceBudgetService{}.listLedger(std::clamp<uint32_t>(limit, 1, 500), filter.vault_id,
                                                   filter.gateway_credential_id);
}

Status status(const Actor& actor, const Filter& filter, const uint32_t limit) {
    requireActor(actor);
    if (filter.vault_id) requireVaultBudgetView(actor, *filter.vault_id);
    else if (!actor->isSuperAdmin() && !ownsCredential(actor, filter.gateway_credential_id))
        throw Denied("system-wide price budget status is for super admins; name a vault or a credential you own");

    budget::PriceBudgetService service;
    service.expireStaleReservations();
    Status out;
    out.trends = service.trendStats(filter.vault_id, filter.gateway_credential_id);
    if (filter.gateway_credential_id)
        std::erase_if(out.trends, [&](const auto& t) { return t.gateway_credential_id != filter.gateway_credential_id; });

    // Without a vault the service lists system-wide notifications and overrides: only super admins see those
    // unfiltered, everyone else the rows for vaults they can view.
    constexpr std::size_t kRows = 20;
    const bool filterByVault = !filter.vault_id && !actor->isSuperAdmin();
    out.notifications = service.listNotifications(filterByVault ? 500 : kRows, filter.vault_id, false);
    out.overrides = service.listOverrides(filterByVault ? 500 : kRows, filter.vault_id, true);
    if (filterByVault) {
        std::erase_if(out.notifications, [&](const auto& n) { return !n.vault_id || !canViewVaultBudget(actor, *n.vault_id); });
        std::erase_if(out.overrides, [&](const auto& o) { return !canViewVaultBudget(actor, o.vault_id); });
        if (out.notifications.size() > kRows) out.notifications.resize(kRows);
        if (out.overrides.size() > kRows) out.overrides.resize(kRows);
    }
    out.policies = listPolicies(actor, filter);
    out.ledger = budget::PriceBudgetService{}.listLedger(std::clamp<uint32_t>(limit, 1, 500), filter.vault_id,
                                                         filter.gateway_credential_id);
    return out;
}

}
