#pragma once

#include <cstdint>
#include <string>

namespace vh::fs::model::stats {

struct Extension {
    std::string extension;   // e.g. "jpg"
    std::uint64_t total_bytes{};
};

}
