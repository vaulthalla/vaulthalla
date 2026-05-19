#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace pqxx { class row; }

namespace vh::email {

struct DeliveryRecordInput {
    std::string eventKey;
    std::string eventType;
    std::string severity;
    std::string provider;
    std::string subject;
    std::optional<std::string> recipientGroup;
    std::uint32_t recipientCount = 0;
    std::optional<std::string> providerMessageId;
    std::string status;
    std::optional<std::string> errorSummary;
    std::string fingerprint;
};

struct DeliveryRecord {
    std::uint64_t id = 0;
    std::string eventKey;
    std::string eventType;
    std::string severity;
    std::string provider;
    std::string subject;
    std::optional<std::string> recipientGroup;
    std::uint32_t recipientCount = 0;
    std::optional<std::string> providerMessageId;
    std::string status;
    std::optional<std::string> errorSummary;
    std::string fingerprint;
    std::time_t firstSeenAt = 0;
    std::time_t lastSeenAt = 0;
    std::optional<std::time_t> sentAt;
    std::time_t createdAt = 0;

    DeliveryRecord() = default;
    explicit DeliveryRecord(const pqxx::row& row);
};

struct DeliveryHistory {
    static std::uint64_t record(const DeliveryRecordInput& input);
    static std::vector<DeliveryRecord> recent(std::uint32_t limit = 100);
    static std::optional<DeliveryRecord> latestFor(const std::string& eventKey, const std::string& fingerprint);
    static std::optional<DeliveryRecord> latestForStatus(
        const std::string& eventKey,
        const std::string& fingerprint,
        const std::string& status
    );
    static std::optional<DeliveryRecord> latestForEventStatus(const std::string& eventKey, const std::string& status);
};

}
