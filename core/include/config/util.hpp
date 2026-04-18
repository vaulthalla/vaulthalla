#pragma once

#include "log/Rotator.hpp"

#include <string>
#include <chrono>
#include <stdexcept>

namespace vh::config {

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
