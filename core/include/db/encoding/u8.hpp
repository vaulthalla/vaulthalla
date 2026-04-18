#pragma once

#include <string>
#include <string_view>

inline std::ostream& operator<<(std::ostream& os, const std::u8string& u8str) {
    const std::string_view sv{reinterpret_cast<const char*>(u8str.data()), u8str.size()};
    return os << sv;
}

namespace vh::db::encoding {

inline std::string to_utf8_string(const std::u8string& u8) {
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

}
