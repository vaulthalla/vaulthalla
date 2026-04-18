#pragma once

#include "db/model/ListQueryParams.hpp"

#include <vector>

namespace vh::db::template_ {

template <typename T>
    std::vector<T> paginate(std::vector<T>&& items, const model::ListQueryParams& params) {
    if (params.page && *params.page >= items.size()) return {};

    const auto offset = params.page ? *params.page : 0;
    const auto limit = params.limit ? *params.limit : 0;
    const auto begin = items.begin() + offset;
    auto end = items.end();

    if (limit > 0 && offset + limit < items.size()) end = items.begin() + offset + limit;

    return std::vector<T>(begin, end);
}

}
