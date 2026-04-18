#pragma once

#include <string>
#include <optional>
#include <sstream>

namespace vh::db::model {

enum class SortDirection { ASC, DESC };

struct ListQueryParams {
    std::optional<std::string> sort;
    std::optional<SortDirection> direction;
    std::optional<std::string> filter;
    std::optional<uint64_t> limit;
    std::optional<uint64_t> page;
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

inline std::string appendPaginationAndFilter(const std::string_view base,
                                      const ListQueryParams& p,
                                      const std::optional<std::string>& defaultSort = std::nullopt,
                                      const std::optional<std::string>& filterCol = std::nullopt) {
    std::ostringstream out;
    out << base;

    if (p.filter && filterCol) out << " WHERE " << *filterCol << " ILIKE '%" << escape(*p.filter) << "%'";

    if (p.sort) {
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
