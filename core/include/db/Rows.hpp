#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <pqxx/result>
#include <pqxx/row>
#include <type_traits>
#include <vector>

// Result -> model mapping, sized once. Rows are visited as pqxx::row_ref, a view into `res`: a mapper copies what it
// needs out of the row (every model constructor does) and never keeps the row_ref itself.
namespace vh::db {

template <typename Fn>
    requires std::invocable<Fn&, pqxx::row_ref>
[[nodiscard]] auto mapRows(const pqxx::result& res, Fn&& fn) {
    std::vector<std::remove_cvref_t<std::invoke_result_t<Fn&, pqxx::row_ref>>> out;
    out.reserve(res.size());
    for (const auto row : res) out.push_back(std::invoke(fn, row));
    return out;
}

// One T per row, constructed from it.
template <typename T>
    requires std::constructible_from<T, pqxx::row_ref>
[[nodiscard]] std::vector<T> rowsAs(const pqxx::result& res) {
    std::vector<T> out;
    out.reserve(res.size());
    for (const auto row : res) out.emplace_back(row);
    return out;
}

// One std::shared_ptr<T> per row, constructed from it.
template <typename T>
    requires std::constructible_from<T, pqxx::row_ref>
[[nodiscard]] std::vector<std::shared_ptr<T>> sharedRows(const pqxx::result& res) {
    return mapRows(res, [](const pqxx::row_ref row) { return std::make_shared<T>(row); });
}

}
