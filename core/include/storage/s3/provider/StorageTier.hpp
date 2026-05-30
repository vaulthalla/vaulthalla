#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace vh::storage::s3::provider {

struct StorageTier {
    std::string id;
    std::string display_name;
    std::optional<std::string> wire_class;
    bool selectable{false};
    bool immediate_read{true};
    bool retrieval_fee_possible{false};
    std::optional<unsigned int> minimum_storage_duration_days;
    std::optional<std::uint64_t> minimum_billable_object_size_bytes;
};

struct TierResolution {
    bool ok{false};
    std::optional<StorageTier> resolved;
    std::optional<std::string> normalized_id;
    std::string error;
};

[[nodiscard]] std::string trimStorageTierValue(const std::string& value);
[[nodiscard]] std::string normalizeTierAliasKey(const std::string& value);
[[nodiscard]] bool containsControlCharacter(const std::string& value);
[[nodiscard]] bool isProviderDefaultTierValue(const std::string& value);

} // namespace vh::storage::s3::provider
