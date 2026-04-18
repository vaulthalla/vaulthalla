#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace vh::vault::model {

struct Activity {
    uint64_t uploads_24hr, uploads_7d, deletes_24hr, deletes_7d, renames_24hr, renames_7d;
    std::time_t last_activity_time;
    std::string last_activity_action;
};

}
