#pragma once

#include <pqxx/row>
#include <string_view>
#include <vector>

namespace vh::db::encoding {
    inline bool hasColumn(const pqxx::row &row, const std::string_view name) {
        return std::ranges::any_of(row, [&name](const auto& field) { return field.name() == name; });
    }

    template<typename T>
    std::optional<T> try_get(const pqxx::row &row, const std::string_view name) {
        for (const auto &f: row)
            if (f.name() == name)
                return f.is_null() ? std::nullopt : std::make_optional(f.as<T>());
        return std::nullopt;
    }

    template<typename T>
    std::optional<T> try_get(const pqxx::row& row, const std::vector<std::string_view>& names) {
        for (const auto& name : names)
            if (const auto res = try_get<T>(row, name)) return res;
        return std::nullopt;
    }
}
