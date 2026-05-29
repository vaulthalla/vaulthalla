#include "protocols/shell/commands/all.hpp"

#include "db/query/vault/Vault.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "usage/include/UsageManager.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>

namespace vh::protocols::shell::commands {
namespace {

using vh::storage::s3::pricing::PriceBudgetLedgerEntry;
using vh::storage::s3::pricing::PriceBudgetMode;
using vh::storage::s3::pricing::PriceBudgetPolicy;
using vh::storage::s3::pricing::PriceBudgetScope;
using vh::storage::s3::pricing::PriceBudgetService;
using vh::vault::model::VaultType;

CommandResult requirePricingSuperAdmin(const CommandCall& call) {
    if (call.user && call.user->isSuperAdmin()) return {};
    return invalid("pricing budget: insufficient permissions; requires super-admin");
}

std::string valueOrDash(const std::optional<std::string>& value) {
    return value && !value->empty() ? *value : "-";
}

std::string valueOrDash(const std::optional<std::uint32_t>& value) {
    return value ? std::to_string(*value) : "-";
}

std::string boolText(const bool value) {
    return value ? "yes" : "no";
}

std::optional<std::uint32_t> resolveVaultId(const std::string& value, const CommandCall& call, std::string& error) {
    std::shared_ptr<vh::vault::model::Vault> vault;
    if (const auto id = parseUInt(value)) vault = vh::db::query::vault::Vault::getVault(*id);
    else if (call.user) vault = vh::db::query::vault::Vault::getVault(value, call.user->id);

    if (!vault) {
        error = "pricing budget: vault not found: " + value;
        return std::nullopt;
    }
    if (vault->type != VaultType::S3) {
        error = "pricing budget: vault price budgets are only available for S3 vaults";
        return std::nullopt;
    }
    return vault->id;
}

std::optional<std::string> parseProviderKey(const std::string& value, std::string& error) {
    auto provider = value;
    std::ranges::transform(provider, provider.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!vh::storage::s3::pricing::isSupportedPriceBudgetProvider(provider)) {
        error = "pricing budget: unsupported provider key for price budgets: " + value +
            " (supported: aws-s3, cloudflare-r2)";
        return std::nullopt;
    }
    return provider;
}

bool assignDecimalOpt(
    const CommandCall& call,
    const std::string& option,
    std::optional<std::string>& target,
    std::string& error) {
    const auto value = optVal(call, option);
    if (!value) return true;
    if (!vh::storage::s3::pricing::isValidPriceBudgetDecimal(*value)) {
        error = "pricing budget: --" + option + " must be a non-negative decimal with at most 8 fractional digits";
        return false;
    }
    target = *value;
    return true;
}

std::optional<std::int64_t> parseMaxCatalogAge(const CommandCall& call, std::string& error) {
    const auto value = optVal(call, "max-catalog-age");
    if (!value) return 43200;
    const auto parsed = parseUInt(*value);
    if (!parsed || *parsed == 0) {
        error = "pricing budget: --max-catalog-age must be a positive integer number of seconds";
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*parsed);
}

std::optional<PriceBudgetPolicy> parsePolicyOptions(
    const CommandCall& call,
    PriceBudgetScope scope,
    std::optional<std::string> providerKey,
    std::optional<std::uint32_t> vaultId,
    std::string& error) {
    PriceBudgetPolicy policy;
    policy.scope = scope;
    policy.provider_key = std::move(providerKey);
    policy.vault_id = vaultId;
    policy.mode = PriceBudgetMode::Report;
    policy.currency = "USD";
    policy.require_verified_catalog = !hasFlag(call, "no-require-verified-catalog");
    policy.allow_stale_catalog = hasFlag(call, "allow-stale-catalog");
    policy.max_catalog_age_seconds = parseMaxCatalogAge(call, error);
    if (!error.empty()) return std::nullopt;

    if (const auto mode = optVal(call, "mode")) {
        try {
            policy.mode = vh::storage::s3::pricing::priceBudgetModeFromString(*mode);
        } catch (const std::exception& e) {
            error = "pricing budget: " + std::string(e.what());
            return std::nullopt;
        }
    }

    if (const auto currency = optVal(call, "currency")) {
        policy.currency = vh::storage::s3::pricing::normalizePriceBudgetCurrency(*currency);
        if (!vh::storage::s3::pricing::isValidPriceBudgetCurrency(policy.currency)) {
            error = "pricing budget: --currency must be 3-8 alphanumeric characters";
            return std::nullopt;
        }
    }

    if (!assignDecimalOpt(call, "max-run", policy.max_run_cost, error) ||
        !assignDecimalOpt(call, "max-daily", policy.max_daily_cost, error) ||
        !assignDecimalOpt(call, "max-monthly", policy.max_monthly_cost, error))
        return std::nullopt;

    return policy;
}

std::string renderPolicies(const std::vector<PriceBudgetPolicy>& policies) {
    if (policies.empty()) return "No S3 price budget policies configured.\n";

    Table table({
        {"ID", Align::Right, 2, 6, false, false},
        {"Scope", Align::Left, 5, 10, false, false},
        {"Provider", Align::Left, 1, 16, false, false},
        {"Vault", Align::Right, 1, 8, false, false},
        {"Mode", Align::Left, 3, 8, false, false},
        {"Currency", Align::Left, 3, 8, false, false},
        {"Run", Align::Right, 1, 14, false, false},
        {"Daily", Align::Right, 1, 14, false, false},
        {"Monthly", Align::Right, 1, 14, false, false},
        {"Verified", Align::Left, 3, 8, false, false},
        {"Stale", Align::Left, 3, 6, false, false},
        {"Active", Align::Left, 3, 6, false, false}
    });

    for (const auto& policy : policies) {
        table.add_row({
            std::to_string(policy.id),
            vh::storage::s3::pricing::toString(policy.scope),
            valueOrDash(policy.provider_key),
            valueOrDash(policy.vault_id),
            vh::storage::s3::pricing::toString(policy.mode),
            policy.currency,
            valueOrDash(policy.max_run_cost),
            valueOrDash(policy.max_daily_cost),
            valueOrDash(policy.max_monthly_cost),
            boolText(policy.require_verified_catalog),
            policy.allow_stale_catalog ? "allow" : "deny",
            boolText(policy.is_active)
        });
    }
    return table.render();
}

std::string renderLedger(const std::vector<PriceBudgetLedgerEntry>& entries) {
    if (entries.empty()) return "No S3 price budget ledger rows.\n";

    Table table({
        {"ID", Align::Right, 2, 6, false, false},
        {"Policy", Align::Right, 2, 6, false, false},
        {"Vault", Align::Right, 2, 8, false, false},
        {"Provider", Align::Left, 1, 16, false, false},
        {"Window", Align::Left, 5, 9, false, false},
        {"Reserved", Align::Right, 1, 14, false, false},
        {"Committed", Align::Right, 1, 14, false, false},
        {"Currency", Align::Left, 3, 8, false, false},
        {"Status", Align::Left, 5, 10, false, false},
        {"Run", Align::Left, 8, 14, false, true}
    });

    for (const auto& entry : entries) {
        table.add_row({
            std::to_string(entry.id),
            std::to_string(entry.policy_id),
            std::to_string(entry.vault_id),
            entry.provider_key,
            vh::storage::s3::pricing::toString(entry.window),
            entry.reserved_cost,
            valueOrDash(entry.committed_cost),
            entry.currency,
            entry.status,
            entry.run_uuid
        });
    }
    return table.render();
}

CommandResult handleBudgetList(const CommandCall&) {
    return ok(renderPolicies(PriceBudgetService{}.listPolicies(true)));
}

CommandResult setPolicy(
    const CommandCall& call,
    const PriceBudgetScope scope,
    std::optional<std::string> providerKey,
    std::optional<std::uint32_t> vaultId) {
    if (auto denied = requirePricingSuperAdmin(call); denied.exit_code != 0) return denied;

    std::string error;
    auto policy = parsePolicyOptions(call, scope, std::move(providerKey), vaultId, error);
    if (!policy) return invalid(error);

    try {
        const auto saved = PriceBudgetService{}.upsertPolicy(*policy);
        return ok("S3 price budget policy saved.\n" + renderPolicies({saved}));
    } catch (const std::exception& e) {
        return invalid("pricing budget: " + std::string(e.what()));
    }
}

CommandResult handleSetGlobal(const CommandCall& call) {
    return setPolicy(call, PriceBudgetScope::Global, std::nullopt, std::nullopt);
}

CommandResult handleSetProvider(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "set-provider"});
    validatePositionals(call, usage);
    std::string error;
    const auto provider = parseProviderKey(call.positionals[0], error);
    if (!provider) return invalid(error);
    return setPolicy(call, PriceBudgetScope::Provider, provider, std::nullopt);
}

CommandResult handleSetVault(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "set-vault"});
    validatePositionals(call, usage);
    std::string error;
    const auto vaultId = resolveVaultId(call.positionals[0], call, error);
    if (!vaultId) return invalid(error);
    return setPolicy(call, PriceBudgetScope::Vault, std::nullopt, vaultId);
}

CommandResult disablePolicy(
    const CommandCall& call,
    const PriceBudgetScope scope,
    const std::optional<std::string>& providerKey,
    const std::optional<std::uint32_t>& vaultId) {
    if (auto denied = requirePricingSuperAdmin(call); denied.exit_code != 0) return denied;
    try {
        const bool disabled = PriceBudgetService{}.disablePolicy(scope, providerKey, vaultId);
        return ok(disabled
            ? "S3 price budget policy disabled.\n"
            : "No matching S3 price budget policy was configured.\n");
    } catch (const std::exception& e) {
        return invalid("pricing budget: " + std::string(e.what()));
    }
}

CommandResult handleDisableGlobal(const CommandCall& call) {
    return disablePolicy(call, PriceBudgetScope::Global, std::nullopt, std::nullopt);
}

CommandResult handleDisableProvider(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "disable-provider"});
    validatePositionals(call, usage);
    std::string error;
    const auto provider = parseProviderKey(call.positionals[0], error);
    if (!provider) return invalid(error);
    return disablePolicy(call, PriceBudgetScope::Provider, provider, std::nullopt);
}

CommandResult handleDisableVault(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "disable-vault"});
    validatePositionals(call, usage);
    std::string error;
    const auto vaultId = resolveVaultId(call.positionals[0], call, error);
    if (!vaultId) return invalid(error);
    return disablePolicy(call, PriceBudgetScope::Vault, std::nullopt, vaultId);
}

CommandResult handleStatus(const CommandCall&) {
    PriceBudgetService service;
    service.expireStaleReservations();
    std::ostringstream out;
    out << "S3 price budget policies\n"
        << renderPolicies(service.listPolicies(true))
        << "\nRecent ledger rows\n"
        << renderLedger(service.listLedger(20));
    return ok(out.str());
}

CommandResult handleLedger(const CommandCall& call) {
    auto limit = std::uint32_t{50};
    if (const auto limitOpt = optVal(call, "limit")) {
        const auto parsed = parseUInt(*limitOpt);
        if (!parsed || *parsed == 0) return invalid("pricing budget ledger: --limit must be a positive integer");
        limit = *parsed;
    }
    return ok(renderLedger(PriceBudgetService{}.listLedger(limit)));
}

bool isBudgetMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"pricing", "budget", cmd}, input);
}

CommandResult handleBudget(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isBudgetMatch("list", sub)) return handleBudgetList(subcall);
    if (isBudgetMatch("set-global", sub)) return handleSetGlobal(subcall);
    if (isBudgetMatch("set-provider", sub)) return handleSetProvider(subcall);
    if (isBudgetMatch("set-vault", sub)) return handleSetVault(subcall);
    if (isBudgetMatch("disable-global", sub)) return handleDisableGlobal(subcall);
    if (isBudgetMatch("disable-provider", sub)) return handleDisableProvider(subcall);
    if (isBudgetMatch("disable-vault", sub)) return handleDisableVault(subcall);
    if (isBudgetMatch("status", sub)) return handleStatus(subcall);
    if (isBudgetMatch("ledger", sub)) return handleLedger(subcall);

    return invalid(call.constructFullArgs(), "pricing budget: unknown subcommand: '" + std::string(sub) + "'");
}

CommandResult handlePricing(const CommandCall& call) {
    if (call.positionals.empty() || hasKey(call, "help") || hasKey(call, "h"))
        return usage(call.constructFullArgs());

    const auto [sub, subcall] = descend(call);
    if (isCommandMatch({"pricing", "budget"}, sub)) return handleBudget(subcall);
    return invalid(call.constructFullArgs(), "pricing: unknown subcommand: '" + std::string(sub) + "'");
}

} // namespace

void registerPricingCommands(const std::shared_ptr<Router>& r) {
    const auto usageManager = runtime::Deps::get().shellUsageManager;
    r->registerCommand(usageManager->resolve("pricing"), handlePricing);
}

} // namespace vh::protocols::shell::commands
