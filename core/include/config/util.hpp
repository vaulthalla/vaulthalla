#pragma once

#include "log/Rotator.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace vh::config {

// Config durations: a non-negative integer followed by s, m, h, d or w ("30s", "5m", "12h", "90d", "2w"); a bare
// integer is seconds. Throws std::invalid_argument on anything else.
inline std::chrono::seconds parseDuration(const std::string& str) {
    if (str.empty()) throw std::invalid_argument("duration cannot be empty");
    const auto last = static_cast<unsigned char>(str.back());
    const bool hasUnit = std::isalpha(last) != 0;
    const auto digits = hasUnit ? str.substr(0, str.size() - 1) : str;
    if (digits.empty() || digits.size() > 9 ||
        !std::ranges::all_of(digits, [](const unsigned char c) { return std::isdigit(c) != 0; }))
        throw std::invalid_argument("invalid duration '" + str + "': use a number with s, m, h, d or w (e.g. 5m, 90d)");
    const auto n = static_cast<int64_t>(std::stoll(digits));
    switch (hasUnit ? std::tolower(last) : 's') {
        case 's': return std::chrono::seconds(n);
        case 'm': return std::chrono::minutes(n);
        case 'h': return std::chrono::hours(n);
        case 'd': return std::chrono::days(n);
        case 'w': return std::chrono::weeks(n);
        default: throw std::invalid_argument("invalid duration unit in '" + str + "': use s, m, h, d or w");
    }
}

// The largest unit that states the duration exactly ("5m", "90d", "45s").
inline std::string durationToString(const std::chrono::seconds d) {
    const auto s = d.count();
    if (s != 0 && s % (7 * 86400) == 0) return std::to_string(s / (7 * 86400)) + "w";
    if (s != 0 && s % 86400 == 0) return std::to_string(s / 86400) + "d";
    if (s != 0 && s % 3600 == 0) return std::to_string(s / 3600) + "h";
    if (s != 0 && s % 60 == 0) return std::to_string(s / 60) + "m";
    return std::to_string(s) + "s";
}

inline std::chrono::hours parseHoursFromDayOrHour(const std::string& str) {
    if (str.empty()) throw std::invalid_argument("Interval string cannot be empty");

    if (str.back() == 'd' || str.back() == 'D') {
        const auto days = std::stoul(str.substr(0, str.size() - 1));
        return std::chrono::hours(days * 24);
    }

    if (str.back() == 'h' || str.back() == 'H') {
        const auto hours = std::stoul(str.substr(0, str.size() - 1));
        return std::chrono::hours(hours);
    }

    // Assume hours if no suffix
    const auto hours = std::stoul(str);
    return std::chrono::hours(hours);
}

inline uintmax_t parseMbOrGbToByte(const std::string& str) {
    if (str.empty()) throw std::invalid_argument("Size string cannot be empty");

    if (str.size() > 2 && (str.substr(str.size() - 2) == "GB" || str.substr(str.size() - 2) == "gb")) {
        const auto gb = std::stoull(str.substr(0, str.size() - 2));
        return gb * 1024 * 1024 * 1024;
    }

    if (str.size() > 1 && (str.back() == 'G' || str.back() == 'g')) {
        const auto gb = std::stoull(str.substr(0, str.size() - 1));
        return gb * 1024 * 1024 * 1024;
    }

    if (str.size() > 2 && (str.substr(str.size() - 2) == "MB" || str.substr(str.size() - 2) == "mb")) {
        const auto mb = std::stoull(str.substr(0, str.size() - 2));
        return mb * 1024 * 1024;
    }

    if (str.size() > 1 && (str.back() == 'M' || str.back() == 'm')) {
        const auto mb = std::stoull(str.substr(0, str.size() - 1));
        return mb * 1024 * 1024;
    }

    // Assume MB if no suffix
    const auto mb = std::stoull(str);
    return mb * 1024 * 1024;
}

inline std::string bytesToMbOrGbStr(const uintmax_t bytes) {
    if (bytes % (1024 * 1024 * 1024) == 0) return std::to_string(bytes / (1024 * 1024 * 1024)) + "GB";
    return std::to_string(bytes / (1024 * 1024)) + "MB";
}

inline std::string hoursToDayOrHourStr(const std::chrono::hours& hours) {
    if (hours.count() % 24 == 0) return std::to_string(hours.count() / 24) + "d";
    return std::to_string(hours.count()) + "h";
}

inline log::Rotator::Compression parseCompression(const std::string& str) {
    if (str == "none") return log::Rotator::Compression::None;
    if (str == "gzip") return log::Rotator::Compression::Gzip;
    if (str == "zstd") return log::Rotator::Compression::Zstd;
    throw std::invalid_argument("Invalid compression type: " + str);
}

inline std::string compressionToString(const log::Rotator::Compression c) {
    switch (c) {
        case log::Rotator::Compression::None: return "none";
        case log::Rotator::Compression::Gzip: return "gzip";
        case log::Rotator::Compression::Zstd: return "zstd";
    }
    return "unknown";
}

}
