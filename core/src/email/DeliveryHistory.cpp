#include "email/DeliveryHistory.hpp"

#include "db/Transactions.hpp"
#include "db/encoding/timestamp.hpp"

#include <algorithm>
#include <pqxx/pqxx>
#include <stdexcept>

namespace vh::email {

namespace {

std::optional<std::string> optString(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return row[column].as<std::string>();
}

std::optional<std::time_t> optTimestamp(const pqxx::row& row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return db::encoding::parsePostgresTimestamp(row[column].as<std::string>());
}

void validateInput(const DeliveryRecordInput& input) {
    if (input.eventKey.empty()) throw std::invalid_argument("operator email delivery event key is required");
    if (input.eventType.empty()) throw std::invalid_argument("operator email delivery event type is required");
    if (input.severity.empty()) throw std::invalid_argument("operator email delivery severity is required");
    if (input.provider.empty()) throw std::invalid_argument("operator email delivery provider is required");
    if (input.subject.empty()) throw std::invalid_argument("operator email delivery subject is required");
    if (input.status.empty()) throw std::invalid_argument("operator email delivery status is required");
    if (input.fingerprint.empty()) throw std::invalid_argument("operator email delivery fingerprint is required");
}

std::vector<DeliveryRecord> recordsFromResult(const pqxx::result& rows) {
    std::vector<DeliveryRecord> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.emplace_back(row);
    return out;
}

}

DeliveryRecord::DeliveryRecord(const pqxx::row& row)
    : id(row["id"].as<std::uint64_t>()),
      eventKey(row["event_key"].as<std::string>()),
      eventType(row["event_type"].as<std::string>()),
      severity(row["severity"].as<std::string>()),
      provider(row["provider"].as<std::string>()),
      subject(row["subject"].as<std::string>()),
      recipientGroup(optString(row, "recipient_group")),
      recipientCount(row["recipient_count"].as<std::uint32_t>()),
      providerMessageId(optString(row, "provider_message_id")),
      status(row["status"].as<std::string>()),
      errorSummary(optString(row, "error_summary")),
      fingerprint(row["fingerprint"].as<std::string>()),
      firstSeenAt(db::encoding::parsePostgresTimestamp(row["first_seen_at"].as<std::string>())),
      lastSeenAt(db::encoding::parsePostgresTimestamp(row["last_seen_at"].as<std::string>())),
      sentAt(optTimestamp(row, "sent_at")),
      createdAt(db::encoding::parsePostgresTimestamp(row["created_at"].as<std::string>())) {}

std::uint64_t DeliveryHistory::record(const DeliveryRecordInput& input) {
    validateInput(input);
    return db::Transactions::exec("email::DeliveryHistory::record", [&](pqxx::work& txn) {
        pqxx::params p;
        p.append(input.eventKey);
        p.append(input.eventType);
        p.append(input.severity);
        p.append(input.provider);
        p.append(input.subject);
        p.append(input.recipientGroup);
        p.append(static_cast<int>(input.recipientCount));
        p.append(input.providerMessageId);
        p.append(input.status);
        p.append(input.errorSummary);
        p.append(input.fingerprint);
        const auto res = txn.exec(pqxx::prepped{"operator_notification_delivery.insert"}, p);
        return res.one_field().as<std::uint64_t>();
    });
}

std::vector<DeliveryRecord> DeliveryHistory::recent(std::uint32_t limit) {
    limit = std::clamp<std::uint32_t>(limit == 0 ? 100 : limit, 1, 500);
    return db::Transactions::exec("email::DeliveryHistory::recent", [&](pqxx::work& txn) {
        const auto rows = txn.exec(
            pqxx::prepped{"operator_notification_delivery.recent"},
            pqxx::params{static_cast<int>(limit)}
        );
        return recordsFromResult(rows);
    });
}

std::optional<DeliveryRecord> DeliveryHistory::latestFor(const std::string& eventKey, const std::string& fingerprint) {
    if (eventKey.empty() || fingerprint.empty()) return std::nullopt;
    return db::Transactions::exec("email::DeliveryHistory::latestFor", [&](pqxx::work& txn) -> std::optional<DeliveryRecord> {
        const auto rows = txn.exec(
            pqxx::prepped{"operator_notification_delivery.latest_for_event"},
            pqxx::params{eventKey, fingerprint}
        );
        if (rows.empty()) return std::nullopt;
        return DeliveryRecord(rows.one_row());
    });
}

}
