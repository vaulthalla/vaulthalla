#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace vh::db::encoding {

inline std::string to_hex_bytea(const std::vector<uint8_t>& v) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(2 * v.size() + 2);
    out += "\\x";
    for (uint8_t b : v) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

inline std::vector<uint8_t> from_hex_bytea(const std::string& s) {
    if (s.size() < 2 || s[0] != '\\' || s[1] != 'x') throw std::invalid_argument("Invalid hex bytea format");
    if ((s.size() - 2) % 2 != 0) throw std::invalid_argument("Hex bytea has invalid length");
    std::vector<uint8_t> out;
    out.reserve((s.size() - 2) / 2);
    for (size_t i = 2; i + 1 < s.size(); i += 2) {
        uint8_t byte = 0;
        for (size_t j = 0; j < 2; ++j) {
            char c = s[i + j];
            byte <<= 4;
            if (c >= '0' && c <= '9') byte |= (c - '0');
            else if (c >= 'a' && c <= 'f') byte |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') byte |= (c - 'A' + 10);
            else throw std::invalid_argument("Invalid character in hex bytea");
        }
        out.push_back(byte);
    }
    return out;
}

}
