#include "storage/s3/provider/StorageTier.hpp"

#include <algorithm>
#include <cctype>

namespace vh::storage::s3::provider {

std::string trimStorageTierValue(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) ++begin;

    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;

    return {begin, end};
}

std::string normalizeTierAliasKey(const std::string& value) {
    auto normalized = trimStorageTierValue(value);
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char ch) {
        if (ch == '-') return '_';
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

bool containsControlCharacter(const std::string& value) {
    return std::ranges::any_of(value, [](const unsigned char ch) {
        return std::iscntrl(ch) != 0;
    });
}

bool isProviderDefaultTierValue(const std::string& value) {
    const auto normalized = normalizeTierAliasKey(value);
    return normalized.empty() || normalized == "none" || normalized == "null" ||
           normalized == "default" || normalized == "provider_default";
}

} // namespace vh::storage::s3::provider
