#pragma once

#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vh::db::encoding {

inline std::time_t parsePostgresTimestamp(const std::string& timestampStr) {
    if (timestampStr.size() < 19) throw std::runtime_error("Failed to parse timestamp: " + timestampStr);

    std::string normalized = timestampStr;
    if (normalized[10] == 'T') normalized[10] = ' ';

    std::tm tm = {};
    std::istringstream ss(normalized.substr(0, 19)); // "YYYY-MM-DD HH:MM:SS"
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) throw std::runtime_error("Failed to parse timestamp: " + timestampStr);

    std::time_t ts = timegm(&tm);
    size_t pos = 19;

    if (pos < normalized.size() && normalized[pos] == '.') {
        ++pos;
        while (pos < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[pos]))) ++pos;
    }

    if (pos < normalized.size() && normalized[pos] == 'Z') return ts;
    if (pos >= normalized.size() || (normalized[pos] != '+' && normalized[pos] != '-')) return ts;

    const char sign = normalized[pos++];
    if (pos + 2 > normalized.size() ||
        !std::isdigit(static_cast<unsigned char>(normalized[pos])) ||
        !std::isdigit(static_cast<unsigned char>(normalized[pos + 1]))) {
        throw std::runtime_error("Failed to parse timestamp timezone: " + timestampStr);
    }

    int offsetSeconds = ((normalized[pos] - '0') * 10 + (normalized[pos + 1] - '0')) * 3600;
    pos += 2;

    if (pos < normalized.size() && normalized[pos] == ':') ++pos;
    if (pos + 2 <= normalized.size() &&
        std::isdigit(static_cast<unsigned char>(normalized[pos])) &&
        std::isdigit(static_cast<unsigned char>(normalized[pos + 1]))) {
        offsetSeconds += ((normalized[pos] - '0') * 10 + (normalized[pos + 1] - '0')) * 60;
    }

    return sign == '+' ? ts - offsetSeconds : ts + offsetSeconds;
}

inline std::string timestampToString(const std::time_t ts) {
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&ts), "%Y-%m-%dT%H:%M:%SZ"); // ISO 8601 UTC
    return oss.str();
}

inline std::time_t parseTimestampFromString(const std::string& iso) {
    std::tm tm = {};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return timegm(&tm);
}

inline std::string getCurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    const std::tm tm = *gmtime(&now_c);
    char buffer[17];
    strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &tm);
    return {buffer};
}

inline std::string getDate() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    const std::tm tm = *gmtime(&now_c);
    char buffer[9];
    strftime(buffer, sizeof(buffer), "%Y%m%d", &tm);
    return {buffer};
}

}
