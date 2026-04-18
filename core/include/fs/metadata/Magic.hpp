#pragma once

#include <string>
#include <magic.h>
#include <vector>
#include <cstdint>

namespace vh::fs::metadata {

class Magic {
public:
    Magic();
    ~Magic();

    [[nodiscard]] std::string mime_type(const std::string& path) const;
    [[nodiscard]] std::string mime_type_buffer(const std::string& buffer) const;

    static std::string get_mime_type(const std::string& path);
    static std::string get_mime_type_from_buffer(const std::string& buffer);
    static std::string get_mime_type_from_buffer(const std::vector<uint8_t>& buffer);

private:
    magic_t cookie;
};

}
