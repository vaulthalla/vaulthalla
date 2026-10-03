// `--sort` reaches ORDER BY in db::model::appendPaginationAndFilter, where it can't be a bound parameter. It used
// to be interpolated as given, so `vh user list --sort "id; DROP TABLE users"` ran the second statement.

#include "db/model/ListQueryParams.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace vh::test_list_query_sql {

using db::model::ListQueryParams;
using db::model::appendPaginationAndFilter;

TEST(ListQuerySql, SortAcceptsPlainAndQualifiedColumns) {
    for (const auto* column : {"id", "name", "created_at", "v.name", "Col2"}) {
        ListQueryParams p{.sort = column};
        EXPECT_NE(appendPaginationAndFilter("SELECT * FROM users", p, "id", "name").find(std::string(" ORDER BY ") + column),
                  std::string::npos) << column;
    }
}

TEST(ListQuerySql, SortRejectsAnythingButAColumnName) {
    for (const auto* injected : {"id; DROP TABLE users", "id--", "(SELECT 1)", "id DESC", "1", ".id", "id.", "a..b",
                                 "name,id", "id/**/", ""}) {
        ListQueryParams p{.sort = injected};
        EXPECT_THROW((void)appendPaginationAndFilter("SELECT * FROM users", p, "id", "name"), std::invalid_argument)
            << injected;
    }
}

TEST(ListQuerySql, FilterStaysInsideItsLiteral) {
    ListQueryParams p{.filter = "x' OR '1'='1"};
    const auto sql = appendPaginationAndFilter("SELECT * FROM users", p, "id", "name");
    EXPECT_NE(sql.find("ILIKE '%x'' OR ''1''=''1%'"), std::string::npos) << sql;
}

}
