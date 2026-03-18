#pragma once

#include <pqxx/row>
#include <string_view>

namespace vh::db::encoding {
    inline bool hasColumn(const pqxx::row &row, const std::string_view name) {
        for (const auto &field: row)
            if (field.name() == name)
                return true;
        return false;
    }

    template<typename T>
    std::optional<T> try_get(const pqxx::row &row, std::string_view name) {
        for (const auto &f: row)
            if (f.name() == name)
                return f.is_null() ? std::nullopt : std::make_optional(f.as<T>());
        return std::nullopt;
    }
}
