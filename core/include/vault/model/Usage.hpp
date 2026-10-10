#pragma once

#include <ctime>
#include "db/Fwd.hpp"

namespace vh::vault::model {

struct Usage {
    unsigned int user_id;
    unsigned int storage_volume_id;
    unsigned long long total_bytes;
    unsigned long long used_bytes;
    std::time_t created_at;
    std::time_t updated_at;

    explicit Usage(pqxx::row_ref row);
};

}
