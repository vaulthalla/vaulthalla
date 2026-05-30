#include "storage/s3/pricing/LocalEstimator.hpp"

#include <algorithm>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <iomanip>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>

namespace vh::storage::s3::pricing {
namespace {

using Decimal = boost::multiprecision::cpp_dec_float_50;

constexpr const char* kFreeTierPolicyApply = "apply_free_tiers";
constexpr const char* kFreeTierPolicyIgnoreAccountWide = "ignore_account_wide_free_tiers";
constexpr const char* kConservativeFreeTierUnknown =
    "account-level free tiers intentionally ignored for budget-safe estimate";

const Decimal kGb{"1000000000"};
const Decimal kGib{"1073741824"};
const Decimal kTb{"1000000000000"};
const Decimal kTib{"1099511627776"};
const Decimal kBillingMonthHours{"730"};
const Decimal kBillingMonthDays{"30"};

Decimal decimalFromJson(const nlohmann::json& value) {
    if (value.is_string()) return Decimal(value.get<std::string>());
    if (value.is_number_integer()) return Decimal(value.get<long long>());
    if (value.is_number_unsigned()) return Decimal(value.get<unsigned long long>());
    if (value.is_number_float()) return Decimal(value.dump());
    return Decimal("0");
}

Decimal decimalFromString(const std::string& value) {
    if (value.empty()) return Decimal("0");
    return Decimal(value);
}

Decimal usageMapValue(
    const std::map<std::string, std::string>& values,
    const std::string& storageClass) {
    if (const auto it = values.find(storageClass); it != values.end())
        return decimalFromString(it->second);
    if (const auto it = values.find("default"); it != values.end())
        return decimalFromString(it->second);
    return Decimal("0");
}

std::string formatDecimal(const Decimal& value) {
    if (value == Decimal("0")) return "0";
    std::ostringstream out;
    out << std::fixed << std::setprecision(24) << value;
    auto text = out.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text == "-0") return "0";
    return text.empty() ? std::string{"0"} : text;
}

std::string formatMoney(const Decimal& value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(8) << (value == Decimal("0") ? Decimal("0") : value);
    auto text = out.str();
    if (text == "-0.00000000") return "0.00000000";
    return text;
}

Decimal ceilDecimal(const Decimal& value) {
    using boost::multiprecision::ceil;
    return ceil(value);
}

Decimal normalizeQuantity(const Decimal& value, const std::string& billingUnit) {
    if (billingUnit == "gb") return value / kGb;
    if (billingUnit == "gib") return value / kGib;
    if (billingUnit == "gb_month") return value / kGb / kBillingMonthHours;
    if (billingUnit == "gib_month") return value / kGib / kBillingMonthHours;
    if (billingUnit == "tb_month") return value / kTb / kBillingMonthHours;
    if (billingUnit == "tib_month") return value / kTib / kBillingMonthHours;
    return value;
}

Decimal byteHoursToGbMonth(const Decimal& value) {
    return value / kGb / kBillingMonthHours;
}

Decimal applyRounding(const Decimal& value, const std::string& roundingRule) {
    if (roundingRule == "ceil_to_billing_unit" ||
        roundingRule == "ceil_to_gb" ||
        roundingRule == "ceil_to_gib" ||
        roundingRule == "ceil_to_tb" ||
        roundingRule == "ceil_to_provider_unit")
        return ceilDecimal(value);
    return value;
}

std::string stringValue(const nlohmann::json& object, const char* key, const std::string& fallback = {}) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

std::string billingUnit(const nlohmann::json& meter) {
    const auto value = stringValue(meter, "billing_unit");
    return value.empty() ? stringValue(meter, "unit") : value;
}

Decimal firstTierRate(const nlohmann::json& meter) {
    if (!meter.contains("tiers") || !meter.at("tiers").is_array() || meter.at("tiers").empty())
        return Decimal("0");
    return decimalFromJson(meter.at("tiers").at(0).at("rate"));
}

std::string firstTierRateUnit(const nlohmann::json& meter) {
    if (!meter.contains("tiers") || !meter.at("tiers").is_array() || meter.at("tiers").empty())
        return {};
    return stringValue(meter.at("tiers").at(0), "rate_unit");
}

bool hasTiers(const nlohmann::json& meter) {
    return meter.contains("tiers") && meter.at("tiers").is_array() && !meter.at("tiers").empty();
}

Decimal quantityForMeter(
    const RatingProfile& profile,
    const nlohmann::json& meter,
    const UsageInput& usage) {
    const auto meterType = stringValue(meter, "meter_type");
    const auto unit = billingUnit(meter);
    if (meterType == "fixed_monthly" || meterType == "minimum_monthly")
        return Decimal("1");
    if (meterType == "storage_byte_hours")
        return normalizeQuantity(usageMapValue(usage.storage_byte_hours, profile.storage_class), unit);
    if (meterType == "retrieval_bytes")
        return normalizeQuantity(decimalFromString(usage.retrieval_bytes), unit);
    if (meterType == "egress_bytes") {
        const auto egress = decimalFromString(usage.egress_bytes);
        return normalizeQuantity(
            egress == Decimal("0") ? decimalFromString(usage.downloaded_bytes) : egress,
            unit);
    }
    if (meterType == "downloaded_bytes")
        return normalizeQuantity(decimalFromString(usage.downloaded_bytes), unit);
    if (meterType == "uploaded_bytes")
        return normalizeQuantity(decimalFromString(usage.uploaded_bytes), unit);
    if (meterType == "object_count_hours") {
        const auto hours = usageMapValue(usage.object_count_hours, profile.storage_class);
        if (unit == "segment_month") return hours / kBillingMonthHours;
        return hours;
    }
    return Decimal("0");
}

Decimal storageGbMonth(const RatingProfile& profile, const UsageInput& usage) {
    return byteHoursToGbMonth(usageMapValue(usage.storage_byte_hours, profile.storage_class));
}

std::optional<Decimal> ruleDecimal(const nlohmann::json& meter, const char* key) {
    if (!meter.contains("rules") || !meter.at("rules").is_object()) return std::nullopt;
    const auto it = meter.at("rules").find(key);
    if (it == meter.at("rules").end() || it->is_null()) return std::nullopt;
    return decimalFromJson(*it);
}

Decimal applyMeterMinimums(
    const RatingProfile& profile,
    const nlohmann::json& meter,
    Decimal quantity,
    const UsageInput& usage,
    std::vector<std::string>& notes) {
    const auto meterType = stringValue(meter, "meter_type");
    if (meterType == "storage_byte_hours" &&
        !usage.object_count_hours.empty() &&
        profile.raw.contains("storage_rules") &&
        profile.raw.at("storage_rules").is_array()) {
        for (const auto& rule : profile.raw.at("storage_rules")) {
            const auto it = rule.find("minimum_billable_object_size_bytes");
            if (it == rule.end() || it->is_null()) continue;
            const auto minimum = decimalFromJson(*it);
            const auto objectHours = usageMapValue(usage.object_count_hours, profile.storage_class);
            const auto minQuantity = normalizeQuantity(objectHours * minimum, billingUnit(meter));
            if (minQuantity > quantity) {
                notes.push_back("minimum billable object size applied: " + formatDecimal(minimum) + " bytes");
                quantity = minQuantity;
            }
        }
    }

    if (meterType == "retrieval_bytes") {
        const auto minimum = ruleDecimal(meter, "minimum_retrieval_object_size_bytes");
        const auto objectCount = decimalFromString(usage.retrieval_object_count);
        if (minimum && objectCount != Decimal("0")) {
            const auto minQuantity = normalizeQuantity(*minimum * objectCount, billingUnit(meter));
            if (minQuantity > quantity) {
                notes.push_back("minimum retrieval object size applied: " + formatDecimal(*minimum) + " bytes");
                quantity = minQuantity;
            }
        }
    }

    return quantity;
}

Decimal applyEgressInclusion(
    const nlohmann::json& meter,
    const Decimal& quantity,
    const Decimal& storageGbMonthForEgress,
    std::vector<std::string>& notes) {
    const auto multiple = ruleDecimal(meter, "included_egress_multiple_of_storage");
    if (stringValue(meter, "meter_type") != "egress_bytes" || !multiple)
        return quantity;
    const auto included = storageGbMonthForEgress * *multiple;
    const auto billable = quantity > included ? quantity - included : Decimal("0");
    if (included > Decimal("0"))
        notes.push_back("included egress applied: " + formatDecimal(included) + " GB");
    return billable;
}

Decimal freeTierOperations(const nlohmann::json& meter) {
    const auto it = meter.find("free_tier_amount");
    if (it == meter.end() || it->is_null()) return Decimal("0");
    const auto amount = decimalFromJson(*it);
    const auto unit = stringValue(meter, "free_tier_unit");
    if (unit == "million_operations") return amount * Decimal("1000000");
    if (unit == "request_1000") return amount * Decimal("1000");
    return amount;
}

Decimal rateQuantity(const Decimal& quantity, const std::string& rateUnit) {
    if (rateUnit == "request_1000") return quantity / Decimal("1000");
    if (rateUnit == "request_10000") return quantity / Decimal("10000");
    if (rateUnit == "million_operations") return quantity / Decimal("1000000");
    return quantity;
}

void addUnknownsFromProfile(const RatingProfile& profile, std::set<std::string>& unknowns) {
    if (!profile.raw.contains("confidence") || !profile.raw.at("confidence").is_object()) return;
    const auto& confidence = profile.raw.at("confidence");
    if (!confidence.contains("unknowns") || !confidence.at("unknowns").is_array()) return;
    for (const auto& item : confidence.at("unknowns"))
        if (item.is_string()) unknowns.insert(item.get<std::string>());
}

nlohmann::json makeBreakdown(
    const std::string& meterKey,
    const Decimal& quantity,
    const Decimal& billableQuantity,
    const std::string& unit,
    const Decimal& rate,
    const std::string& rateUnit,
    const Decimal& cost,
    const std::vector<std::string>& notes) {
    return {
        {"meter_key", meterKey},
        {"quantity", formatDecimal(quantity)},
        {"billable_quantity", formatDecimal(billableQuantity)},
        {"unit", unit},
        {"rate", formatDecimal(rate)},
        {"rate_unit", rateUnit},
        {"cost", formatDecimal(cost)},
        {"notes", notes}
    };
}

std::map<std::string, Decimal> operationMeterQuantities(
    const RatingProfile& profile,
    const UsageInput& usage) {
    std::map<std::string, Decimal> totals;
    if (!profile.raw.contains("operation_map") || !profile.raw.at("operation_map").is_object())
        return totals;

    const auto& operationMap = profile.raw.at("operation_map");
    for (const auto& [operation, countString] : usage.provider_operation_counts) {
        const auto mapping = operationMap.find(operation);
        if (mapping == operationMap.end() || !mapping->is_object()) continue;
        const auto meterKey = stringValue(*mapping, "meter_key");
        if (meterKey.empty()) continue;
        const auto multiplier = mapping->contains("multiplier")
            ? decimalFromJson(mapping->at("multiplier"))
            : Decimal("1");
        totals[meterKey] += decimalFromString(countString) * multiplier;
    }
    return totals;
}

void appendEarlyDeletePenalties(
    const RatingProfile& profile,
    const UsageInput& usage,
    nlohmann::json& breakdown) {
    if (usage.early_delete_gb_days.empty() ||
        !profile.raw.contains("meters") ||
        !profile.raw.at("meters").contains("storage"))
        return;

    const auto& storageMeter = profile.raw.at("meters").at("storage");
    if (!hasTiers(storageMeter)) return;

    const auto gbDays = usageMapValue(usage.early_delete_gb_days, profile.storage_class);
    if (gbDays == Decimal("0")) return;

    auto rate = firstTierRate(storageMeter);
    const auto rateUnit = firstTierRateUnit(storageMeter);
    if (rateUnit == "tb_month" || rateUnit == "tib_month")
        rate /= Decimal("1000");

    Decimal quantity("0");
    std::string unit;
    if (rateUnit == "gib_month") {
        quantity = (gbDays * kGb / kGib) / kBillingMonthDays;
        unit = "gib_month";
    } else {
        quantity = gbDays / kBillingMonthDays;
        unit = "gb_month";
    }
    const auto cost = quantity * rate;
    breakdown.push_back(makeBreakdown(
        "minimum_duration_penalty",
        gbDays,
        quantity,
        unit,
        rate,
        rateUnit,
        cost,
        {"early delete gb-days converted to remaining storage charge"}));
}

} // namespace

EstimateResult LocalEstimator::estimate(
    const RatingProfile& profile,
    const UsageInput& usage,
    const LocalEstimateOptions options) const {
    const bool budgetConservative =
        options.mode == PriceEstimateMode::BudgetConservative || !options.apply_free_tiers;
    const bool applyFreeTiers = !budgetConservative;
    const auto estimateMode = budgetConservative ? "budget_conservative" : "reporting";
    const auto freeTierPolicy = budgetConservative
        ? kFreeTierPolicyIgnoreAccountWide
        : kFreeTierPolicyApply;

    nlohmann::json breakdown = nlohmann::json::array();
    nlohmann::json freeTierApplied = nlohmann::json::object();
    nlohmann::json roundingApplied = nlohmann::json::object();
    std::set<std::string> unknowns;
    addUnknownsFromProfile(profile, unknowns);
    if (!applyFreeTiers) unknowns.insert(kConservativeFreeTierUnknown);

    const auto storageGbMonthForEgress = storageGbMonth(profile, usage);

    if (profile.raw.contains("meters") && profile.raw.at("meters").is_object()) {
        for (const auto& [meterKeyFromMap, meter] : profile.raw.at("meters").items()) {
            if (!meter.is_object() || !hasTiers(meter)) continue;
            const auto meterKey = stringValue(meter, "meter_key", meterKeyFromMap);
            const auto meterType = stringValue(meter, "meter_type");
            auto quantity = quantityForMeter(profile, meter, usage);
            if (quantity == Decimal("0") && meterType != "fixed_monthly" && meterType != "minimum_monthly")
                continue;

            std::vector<std::string> notes;
            quantity = applyMeterMinimums(profile, meter, quantity, usage, notes);
            auto billable = applyEgressInclusion(meter, quantity, storageGbMonthForEgress, notes);

            if (meter.contains("free_tier_amount") && !meter.at("free_tier_amount").is_null()) {
                const auto freeAmount = decimalFromJson(meter.at("free_tier_amount"));
                if (applyFreeTiers) {
                    const auto before = billable;
                    billable = before > freeAmount ? before - freeAmount : Decimal("0");
                    const auto applied = before - billable;
                    if (applied > Decimal("0")) {
                        freeTierApplied[meterKey] = formatDecimal(applied);
                        notes.push_back(
                            "free tier applied: " + formatDecimal(applied) + " " +
                            (stringValue(meter, "free_tier_unit").empty()
                                ? billingUnit(meter)
                                : stringValue(meter, "free_tier_unit")));
                    }
                } else {
                    notes.push_back(
                        "free tier ignored for budget safety: " + formatDecimal(freeAmount) + " " +
                        (stringValue(meter, "free_tier_unit").empty()
                            ? billingUnit(meter)
                            : stringValue(meter, "free_tier_unit")));
                }
            }

            const auto rounded = applyRounding(billable, stringValue(meter, "rounding_rule", "none"));
            if (rounded != billable) {
                roundingApplied[meterKey] = stringValue(meter, "rounding_rule", "none");
                notes.push_back("rounded from " + formatDecimal(billable) + " to " + formatDecimal(rounded));
            }
            billable = rounded;

            const auto rateUnit = firstTierRateUnit(meter);
            const auto rate = firstTierRate(meter);
            const auto cost = rateQuantity(billable, rateUnit) * rate;
            breakdown.push_back(makeBreakdown(
                meterKey,
                quantity,
                billable,
                billingUnit(meter),
                rate,
                rateUnit,
                cost,
                notes));
        }

        const auto operationTotals = operationMeterQuantities(profile, usage);
        for (const auto& [meterKey, quantity] : operationTotals) {
            const auto meterIt = profile.raw.at("meters").find(meterKey);
            if (meterIt == profile.raw.at("meters").end() || !meterIt->is_object() || !hasTiers(*meterIt)) {
                unknowns.insert("operation meter " + meterKey + " not present in profile");
                continue;
            }
            if (quantity == Decimal("0")) continue;

            const auto& meter = *meterIt;
            std::vector<std::string> notes;
            auto billable = quantity;
            if (meter.contains("free_tier_amount") && !meter.at("free_tier_amount").is_null()) {
                const auto freeAmount = freeTierOperations(meter);
                if (applyFreeTiers) {
                    const auto before = billable;
                    billable = before > freeAmount ? before - freeAmount : Decimal("0");
                    const auto applied = before - billable;
                    if (applied > Decimal("0")) {
                        const auto current = freeTierApplied.contains(meterKey)
                            ? decimalFromJson(freeTierApplied.at(meterKey))
                            : Decimal("0");
                        freeTierApplied[meterKey] = formatDecimal(current + applied);
                        notes.push_back("free tier applied: " + formatDecimal(applied) + " operations");
                    }
                } else {
                    notes.push_back("free tier ignored for budget safety: " + formatDecimal(freeAmount) + " operations");
                }
            }

            const auto rateUnit = firstTierRateUnit(meter);
            const auto normalized = rateQuantity(billable, rateUnit);
            const auto rounded = applyRounding(normalized, stringValue(meter, "rounding_rule", "none"));
            if (rounded != normalized) {
                roundingApplied[meterKey] = stringValue(meter, "rounding_rule", "none");
                notes.push_back("rounded from " + formatDecimal(normalized) + " to " + formatDecimal(rounded));
            }
            const auto rate = firstTierRate(meter);
            const auto cost = rounded * rate;
            breakdown.push_back(makeBreakdown(
                meterKey,
                quantity,
                rounded,
                rateUnit,
                rate,
                rateUnit,
                cost,
                notes));
        }
    }

    appendEarlyDeletePenalties(profile, usage, breakdown);

    Decimal total("0");
    for (const auto& item : breakdown)
        if (item.contains("cost")) total += decimalFromJson(item.at("cost"));

    if (profile.raw.contains("provenance") && profile.raw.at("provenance").is_object()) {
        const auto it = profile.raw.at("provenance").find("minimum_monthly_usd");
        if (it != profile.raw.at("provenance").end() && !it->is_null()) {
            const auto minimumMonthly = decimalFromJson(*it);
            if (total < minimumMonthly) {
                const auto delta = minimumMonthly - total;
                breakdown.push_back(makeBreakdown(
                    "minimum_monthly",
                    Decimal("1"),
                    Decimal("1"),
                    "month",
                    delta,
                    "month",
                    delta,
                    {"minimum monthly charge enforced at " + formatDecimal(minimumMonthly)}));
                total = minimumMonthly;
            }
        }
    }

    if (!profile.raw.contains("meters") || !profile.raw.at("meters").is_object() || profile.raw.at("meters").empty())
        unknowns.insert("profile has no deterministic meters; operator manual pricing required");

    std::vector<std::string> unknownList(unknowns.begin(), unknowns.end());
    nlohmann::json raw = {
        {"estimated_cost", formatMoney(total)},
        {"currency", profile.raw.contains("scope") && profile.raw.at("scope").is_object()
            ? profile.raw.at("scope").value("currency", "USD")
            : "USD"},
        {"estimate_mode", estimateMode},
        {"free_tier_policy", freeTierPolicy},
        {"free_tiers_applied", !freeTierApplied.empty()},
        {"breakdown", breakdown},
        {"free_tier_applied", freeTierApplied},
        {"rounding_applied", roundingApplied},
        {"confidence", profile.raw.value("confidence", nlohmann::json::object())},
        {"unknowns", unknownList}
    };

    return EstimateResult::parse(raw);
}

} // namespace vh::storage::s3::pricing
