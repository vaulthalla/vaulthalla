#pragma once

#include <string>

namespace vh::sync::model {

struct EntryKey {
    std::u8string rel; // normalized relative path: no leading '/', consistent separators
    friend auto operator<=>(const EntryKey&, const EntryKey&) = default;
};

struct CompareResult {
    bool equalContent;
    bool localNewer;
    bool remoteNewer;
};

}
