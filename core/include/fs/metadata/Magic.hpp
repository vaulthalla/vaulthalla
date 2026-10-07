#pragma once

#include <string>
#include <magic.h>
#include <vector>
#include <cstdint>
#include <mutex>

namespace vh::fs::metadata {

// A magic_t cookie is not thread-safe: every call on one cookie is serialized (concurrent FUSE seals and uploads
// shared the static cookies unguarded and corrupted the heap).
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
    mutable std::mutex mutex_;
};

}
