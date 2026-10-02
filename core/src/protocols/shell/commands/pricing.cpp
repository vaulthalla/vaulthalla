#include "protocols/shell/commands/all.hpp"

#include "db/query/vault/Vault.hpp"
#include "identities/User.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/commands/helpers.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "ops/Pricing.hpp"
#include "protocols/shell/util/runOp.hpp"
#include "runtime/Deps.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "usage/include/UsageManager.hpp"
#include "vault/model/Vault.hpp"

#include <functional>
#include <optional>
#include <sstream>
#include <string>

namespace vh::protocols::shell::commands {
namespace {

using vh::storage::s3::pricing::PriceBudgetLedgerEntry;
using vh::storage::s3::pricing::PriceBudgetPolicy;
using vh::storage::s3::pricing::PriceBudgetScope;

std::string valueOrDash(const std::optional<std::string>& value) {
    return value && !value->empty() ? *value : "-";
}

std::string valueOrDash(const std::optional<std::uint32_t>& value) {
    return value ? std::to_string(*value) : "-";
}

std::string boolText(const bool value) {
    return value ? "yes" : "no";
}

// A vault by id, or by name among the caller's own vaults. Whether it may carry a budget is ops::pricing's call.
std::uint32_t vaultIdArg(const std::string& value, const CommandCall& call) {
    if (const auto id = parseUInt(value)) return *id;
    const auto vault = vh::db::query::vault::Vault::getVault(value, call.user->id);
    if (!vault) throw vh::ops::NotFound("vault not found: " + value);
    return vault->id;
}

vh::ops::pricing::PolicySpec specFromOptions(const CommandCall& call, const PriceBudgetScope scope) {
    vh::ops::pricing::PolicySpec spec{.scope = scope, .mode = optVal(call, "mode"), .currency = optVal(call, "currency"),
                                      .max_run_cost = optVal(call, "max-run"), .max_daily_cost = optVal(call, "max-daily"),
                                      .max_monthly_cost = optVal(call, "max-monthly"),
                                      .require_verified_catalog = !hasFlag(call, "no-require-verified-catalog"),
                                      .allow_stale_catalog = hasFlag(call, "allow-stale-catalog")};
    if (const auto age = optVal(call, "max-catalog-age")) {
        const auto parsed = parseUInt(*age);
        if (!parsed || *parsed == 0)
            throw vh::ops::Invalid("--max-catalog-age must be a positive integer number of seconds");
        spec.max_catalog_age_seconds = static_cast<std::int64_t>(*parsed);
    }
    return spec;
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

std::string renderBudgetLedger(const std::vector<PriceBudgetLedgerEntry>& entries) {
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

CommandResult handleBudgetList(const CommandCall& call) {
    return runOp("pricing budget", [&] { return vh::ops::pricing::listPolicies(call.user); },
                 [](const std::vector<PriceBudgetPolicy>& policies) { return renderPolicies(policies); });
}

CommandResult handleSetGlobal(const CommandCall& call) {
    return runOp("pricing budget", [&] { return vh::ops::pricing::upsertPolicy(call.user, specFromOptions(call, PriceBudgetScope::Global)); },
                 [](const PriceBudgetPolicy& saved) { return "S3 price budget policy saved.\n" + renderPolicies({saved}); });
}

CommandResult handleSetProvider(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "set-provider"});
    validatePositionals(call, usage);
    return runOp("pricing budget", [&] {
        auto spec = specFromOptions(call, PriceBudgetScope::Provider);
        spec.provider_key = call.positionals[0];
        return vh::ops::pricing::upsertPolicy(call.user, spec);
    }, [](const PriceBudgetPolicy& saved) { return "S3 price budget policy saved.\n" + renderPolicies({saved}); });
}

CommandResult handleSetVault(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "set-vault"});
    validatePositionals(call, usage);
    return runOp("pricing budget", [&] {
        auto spec = specFromOptions(call, PriceBudgetScope::Vault);
        spec.vault_id = vaultIdArg(call.positionals[0], call);
        return vh::ops::pricing::upsertPolicy(call.user, spec);
    }, [](const PriceBudgetPolicy& saved) { return "S3 price budget policy saved.\n" + renderPolicies({saved}); });
}

CommandResult disablePolicy(const CommandCall& call, const PriceBudgetScope scope,
                            const std::function<std::optional<std::string>()>& provider,
                            const std::function<std::optional<std::uint32_t>()>& vault) {
    return runOp("pricing budget", [&] { return vh::ops::pricing::disablePolicy(call.user, scope, provider(), vault()); },
                 [](const bool disabled) {
                     return std::string(disabled ? "S3 price budget policy disabled.\n"
                                                 : "No matching S3 price budget policy was configured.\n");
                 });
}

CommandResult handleDisableGlobal(const CommandCall& call) {
    return disablePolicy(call, PriceBudgetScope::Global, [] { return std::nullopt; }, [] { return std::nullopt; });
}

CommandResult handleDisableProvider(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "disable-provider"});
    validatePositionals(call, usage);
    return disablePolicy(call, PriceBudgetScope::Provider,
                         [&] { return std::make_optional(call.positionals[0]); }, [] { return std::nullopt; });
}

CommandResult handleDisableVault(const CommandCall& call) {
    const auto usage = resolveUsage({"pricing", "budget", "disable-vault"});
    validatePositionals(call, usage);
    return disablePolicy(call, PriceBudgetScope::Vault, [] { return std::nullopt; },
                         [&] { return std::make_optional(vaultIdArg(call.positionals[0], call)); });
}

CommandResult handleStatus(const CommandCall& call) {
    return runOp("pricing budget", [&] { return vh::ops::pricing::status(call.user, {}, 20); },
                 [](const vh::ops::pricing::Status& status) {
                     std::ostringstream out;
                     out << "S3 price budget policies\n" << renderPolicies(status.policies)
                         << "\nRecent ledger rows\n" << renderBudgetLedger(status.ledger);
                     return out.str();
                 });
}

CommandResult handleLedger(const CommandCall& call) {
    auto limit = std::uint32_t{50};
    if (const auto limitOpt = optVal(call, "limit")) {
        const auto parsed = parseUInt(*limitOpt);
        if (!parsed || *parsed == 0) return invalid("pricing budget ledger: --limit must be a positive integer");
        limit = *parsed;
    }
    return runOp("pricing budget ledger", [&] { return vh::ops::pricing::ledger(call.user, {}, limit); },
                 [](const std::vector<PriceBudgetLedgerEntry>& entries) { return renderBudgetLedger(entries); });
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
