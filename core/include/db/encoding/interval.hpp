#pragma once

#include "protocols/shell/util/argsHelpers.hpp"

#include <string>
#include <chrono>
#include <sstream>

namespace vh::db::encoding {

inline std::chrono::seconds parsePostgresInterval(const std::string& s) {
    std::istringstream ss(s);
    int days = 0, hours = 0, minutes = 0, seconds = 0;
    char colon;

    if (s.find("day") != std::string::npos) {
        ss >> days;
        std::string tmp;
        ss >> tmp; // discard "day"
    }

    ss >> hours >> colon >> minutes >> colon >> seconds;

    return std::chrono::seconds(
        days * 86400 +
        hours * 3600 +
        minutes * 60 +
        seconds
    );
}

inline std::string intervalToString(const std::chrono::seconds& interval) {
    const auto days = std::chrono::duration_cast<std::chrono::days>(interval);
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(interval - days);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(interval - days - hours);
    const auto seconds = interval.count() % 60;

    std::ostringstream oss;
    if (days.count() > 0) oss << days.count() << "d ";
    if (hours.count() > 0 || days.count() > 0) oss << hours.count() << "h ";
    if (minutes.count() > 0 || hours.count() > 0 || days.count() > 0) oss << minutes.count() << "m ";
    oss << seconds << "s";

    return oss.str();
}

inline std::chrono::seconds parseSyncInterval(const std::string& intervalStr) {
    if (intervalStr.empty()) return std::chrono::seconds(0); // No interval set

    if (!std::isalpha(intervalStr.back())) {
        // If the last character is not a letter, assume it's a number of seconds
        const auto parsed = protocols::shell::parseUInt(intervalStr);
        if (!parsed || *parsed <= 0) throw std::invalid_argument("vault sync update: --interval must be a positive integer");
        return std::chrono::seconds(*parsed);
    }

    const char unit = std::tolower(intervalStr.back());
    const std::string numStr = intervalStr.substr(0, intervalStr.size() - 1);
    if (numStr.empty()) throw std::invalid_argument("vault sync update: --interval must be a positive integer");

    if (const auto numOpt = protocols::shell::parseUInt(numStr)) {
        if (*numOpt <= 0) throw std::invalid_argument("vault sync update: --interval must be a positive integer");
        if (unit == 's') return std::chrono::seconds(*numOpt);
        if (unit == 'm') return std::chrono::minutes(*numOpt);
        if (unit == 'h') return std::chrono::hours(*numOpt);
        if (unit == 'd') return std::chrono::days(*numOpt);
        throw std::invalid_argument("vault sync update: --interval must be a valid time unit (s, m, h, d)");
    }

    throw std::invalid_argument("vault sync update: --interval must be a positive integer");
}

}
