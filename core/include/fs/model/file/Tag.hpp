#pragma once

#include <ctime>
#include <string>

namespace vh::fs::model::file {

struct Tag {
    unsigned int id;
    std::string name;
};

struct FileTagAssignment {
    unsigned int file_id;
    unsigned int tag_id;
    std::time_t assigned_at;
};

}
