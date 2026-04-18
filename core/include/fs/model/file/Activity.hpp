#pragma once

#include <ctime>

namespace vh::fs::model::file {

struct Activity {
    enum class Action { CREATED, MODIFIED, DELETED };

    unsigned int id, file_id, user_id;
    Action activity = Action::CREATED;
    std::time_t timestamp;
};

}
