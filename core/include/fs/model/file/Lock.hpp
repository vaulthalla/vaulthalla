#pragma once

#include <ctime>
#include <optional>

namespace vh::fs::model::file {

struct Lock {
    unsigned int file_id;
    unsigned int locked_by;
    std::time_t locked_at;
    std::optional<std::time_t> expires_at;
};

}
