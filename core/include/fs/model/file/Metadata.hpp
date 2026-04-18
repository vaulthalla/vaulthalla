#pragma once

#include <ctime>
#include <string>

namespace vh::fs::model::file {
enum class FileMetadataType { String, Integer, Boolean, Timestamp, Float };

struct Metadata {
    unsigned int id;
    unsigned int file_id;
    std::string key;
    FileMetadataType type;
};

struct FileMetadataString {
    unsigned int file_metadata_id;
    std::string value;
};

struct FileMetadataInteger {
    unsigned int file_metadata_id;
    unsigned long value;
};

struct FileMetadataBoolean {
    unsigned int file_metadata_id;
    bool value;
};

struct FileMetadataTimestamp {
    unsigned int file_metadata_id;
    std::time_t value;
};

struct FileMetadataFloat {
    unsigned int file_metadata_id;
    double value;
};

}
