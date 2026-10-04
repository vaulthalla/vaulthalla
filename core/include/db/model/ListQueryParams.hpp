#pragma once

#include <string>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <algorithm>
#include <cstdint>

namespace vh::db::model {

enum class SortDirection { ASC, DESC };

struct ListQueryParams {
    std::optional<std::string> sort{};
    std::optional<SortDirection> direction{};
    std::optional<std::string> filter{};
    std::optional<uint64_t> limit{};
    std::optional<uint64_t> page{};
};

inline std::string to_string(const std::optional<SortDirection>& order) {
    if (!order) return "ASC";
    return order == SortDirection::ASC ? "ASC" : "DESC";
}

static std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

// The sort key lands in ORDER BY, where a bound parameter can't go, so it must be a plain (optionally
// table-qualified) column name. It comes straight from `--sort`; anything else was SQL injection.
inline bool isSortColumn(const std::string_view s) {
    if (s.empty() || s.size() > 63) return false;
    bool segmentStart = true;
    for (const char c : s) {
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (c == '.') {
            if (segmentStart) return false;
            segmentStart = true;
            continue;
        }
        if (segmentStart ? !alpha : !(alpha || digit)) return false;
        segmentStart = false;
    }
    return !segmentStart;
}

inline std::string appendPaginationAndFilter(const std::string_view base,
                                      const ListQueryParams& p,
                                      const std::optional<std::string>& defaultSort = std::nullopt,
                                      const std::optional<std::string>& filterCol = std::nullopt) {
    std::ostringstream out;
    out << base;

    if (p.filter && filterCol) out << " WHERE " << *filterCol << " ILIKE '%" << escape(*p.filter) << "%'";

    if (p.sort) {
        if (!isSortColumn(*p.sort)) throw std::invalid_argument("Invalid sort column: " + *p.sort);
        out << " ORDER BY " << *p.sort;
        out << " " << to_string(p.direction);
    } else if (defaultSort) out << " ORDER BY " << *defaultSort << " ASC";

    const uint64_t page = std::max<int64_t>(p.page.value_or(1), 1);
    const uint64_t limit = p.limit.value_or(100);
    const uint64_t offset = (page - 1) * limit;

    out << " LIMIT " << limit;
    out << " OFFSET " << offset;

    return out.str();
}

}
