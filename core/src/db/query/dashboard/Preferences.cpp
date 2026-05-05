#include "db/query/dashboard/Preferences.hpp"

#include "db/Transactions.hpp"
#include "stats/model/DashboardPreferences.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <stdexcept>

namespace vh::db::query::dashboard {

namespace {

constexpr std::size_t kMaxDashboardPreferenceBytes = 64u * 1024u;
constexpr std::size_t kMaxDashboardPreferenceCards = 64u;

std::string normalizePreferenceKey(std::string preferenceKey) {
    preferenceKey.erase(preferenceKey.begin(), std::find_if(preferenceKey.begin(), preferenceKey.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    preferenceKey.erase(std::find_if(preferenceKey.rbegin(), preferenceKey.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), preferenceKey.end());

    if (preferenceKey.empty()) return "dashboard.home";
    if (preferenceKey.size() > 64) throw std::invalid_argument("Dashboard preference key is too long.");
    return preferenceKey;
}

std::uint64_t optionalEpoch(const pqxx::row& row, const char* column) {
    const auto field = row[column];
    if (field.is_null()) return 0;
    const auto value = field.as<long long>(0);
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

std::shared_ptr<::vh::stats::model::DashboardPreference> preferenceFromRow(
    const std::uint32_t userId,
    const std::string& preferenceKey,
    const pqxx::row* row
) {
    auto preference = std::make_shared<::vh::stats::model::DashboardPreference>();
    preference->userId = userId;
    preference->preferenceKey = preferenceKey;
    preference->layout = nlohmann::json::object();

    if (!row) return preference;

    preference->exists = true;
    preference->id = (*row)["id"].as<std::uint32_t>();
    preference->preferenceKey = (*row)["preference_key"].as<std::string>(preferenceKey);
    const auto rawLayout = (*row)["layout_json"].as<std::string>("{}");
    preference->layout = nlohmann::json::parse(rawLayout, nullptr, false);
    if (!preference->layout.is_object()) preference->layout = nlohmann::json::object();

    const auto created = optionalEpoch(*row, "created_at");
    const auto updated = optionalEpoch(*row, "updated_at");
    if (created > 0) preference->createdAt = created;
    if (updated > 0) preference->updatedAt = updated;

    return preference;
}

void validateLayoutCard(const nlohmann::json& card) {
    if (!card.is_object()) throw std::invalid_argument("Dashboard layout cards must be objects.");

    if (!card.contains("id") || !card.at("id").is_string() || card.at("id").get<std::string>().empty())
        throw std::invalid_argument("Dashboard layout card id is required.");

    if (card.at("id").get<std::string>().size() > 128)
        throw std::invalid_argument("Dashboard layout card id is too long.");

    if (card.contains("size") && !card.at("size").is_null() && !card.at("size").is_string())
        throw std::invalid_argument("Dashboard layout card size must be a string.");

    if (card.contains("variant") && !card.at("variant").is_null() && !card.at("variant").is_string())
        throw std::invalid_argument("Dashboard layout card variant must be a string.");

    if (card.contains("visible") && !card.at("visible").is_null() && !card.at("visible").is_boolean())
        throw std::invalid_argument("Dashboard layout card visibility must be a boolean.");

    if (card.contains("order") && !card.at("order").is_null()) {
        if (!card.at("order").is_number_integer())
            throw std::invalid_argument("Dashboard layout card order must be an integer.");
        if (card.at("order").get<long long>() < 0 || card.at("order").get<long long>() > 100000)
            throw std::invalid_argument("Dashboard layout card order is out of range.");
    }
}

nlohmann::json validateLayout(nlohmann::json layout) {
    if (!layout.is_object()) throw std::invalid_argument("Dashboard preference layout must be an object.");

    const auto dumped = layout.dump();
    if (dumped.size() > kMaxDashboardPreferenceBytes)
        throw std::invalid_argument("Dashboard preference layout is too large.");

    if (const auto cards = layout.find("cards"); cards != layout.end()) {
        if (!cards->is_array()) throw std::invalid_argument("Dashboard preference layout cards must be an array.");
        if (cards->size() > kMaxDashboardPreferenceCards)
            throw std::invalid_argument("Dashboard preference layout has too many cards.");
        for (const auto& card : *cards) validateLayoutCard(card);
    }

    return layout;
}

}

std::shared_ptr<::vh::stats::model::DashboardPreference> Preferences::getForUser(
    const std::uint32_t userId,
    const std::string& preferenceKey
) {
    const auto key = normalizePreferenceKey(preferenceKey);
    return Transactions::exec("DashboardPreferences::getForUser", [&](pqxx::work& txn) {
        const auto rows = txn.exec(pqxx::prepped{"dashboard_preferences.get_for_user"}, pqxx::params{userId, key});
        if (rows.empty()) return preferenceFromRow(userId, key, nullptr);
        const auto row = rows.front();
        return preferenceFromRow(userId, key, &row);
    });
}

std::shared_ptr<::vh::stats::model::DashboardPreference> Preferences::upsertForUser(
    const std::uint32_t userId,
    const std::string& preferenceKey,
    const nlohmann::json& layout
) {
    const auto key = normalizePreferenceKey(preferenceKey);
    const auto cleanLayout = validateLayout(layout);

    return Transactions::exec("DashboardPreferences::upsertForUser", [&](pqxx::work& txn) {
        const auto rows = txn.exec(
            pqxx::prepped{"dashboard_preferences.upsert_for_user"},
            pqxx::params{userId, key, cleanLayout.dump()}
        );
        if (rows.empty()) throw std::runtime_error("Unable to save dashboard preferences.");
        const auto row = rows.front();
        return preferenceFromRow(userId, key, &row);
    });
}

bool Preferences::resetForUser(const std::uint32_t userId, const std::string& preferenceKey) {
    const auto key = normalizePreferenceKey(preferenceKey);
    return Transactions::exec("DashboardPreferences::resetForUser", [&](pqxx::work& txn) {
        const auto rows = txn.exec(pqxx::prepped{"dashboard_preferences.reset_for_user"}, pqxx::params{userId, key});
        return !rows.empty() && rows.front()["deleted"].as<long long>(0) > 0;
    });
}

}
