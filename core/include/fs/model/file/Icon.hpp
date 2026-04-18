#pragma once

#include <ctime>
#include <optional>
#include <string>

namespace vh::fs::model::file {

struct Icon {
    unsigned int file_id;
    std::string name;
    std::optional<std::string> icon_path;
    std::optional<std::string> thumbnail_path;
    std::optional<std::string> preview_path;
    std::time_t generated_at;
};

}
