#pragma once

// Forward declarations for libpqxx. Declarations only: include the defining header to use a
// type. <pqxx/types> covers libpqxx's owning classes (result, row, field, connection, ...); libpqxx 8's non-owning
// views, which every row-mapping signature takes by value, are declared here.

#include <pqxx/types>

namespace pqxx {
    class row_ref;
    class field_ref;
}
