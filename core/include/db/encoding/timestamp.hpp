#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace vh::db::encoding {

inline std::time_t parsePostgresTimestamp(const std::string& timestampStr) {
    std::tm tm = {};
    std::istringstream ss(timestampStr.substr(0, 19)); // truncate to "YYYY-MM-DD HH:MM:SS"
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) throw std::runtime_error("Failed to parse timestamp: " + timestampStr);
    return timegm(&tm); // returns UTC-based time_t
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
