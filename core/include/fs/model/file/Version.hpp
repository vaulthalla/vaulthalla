#pragma once

#include <ctime>
#include <string>

namespace vh::fs::model::file {

struct Version {
    unsigned int id;
    unsigned int file_id;
    unsigned int version_number;
    std::string content_hash;
    unsigned long size_bytes;
    std::string mime_type;
    std::string storage_path;
    std::time_t created_at;
    unsigned int created_by;
};

}
