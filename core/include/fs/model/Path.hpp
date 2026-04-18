#pragma once

#include <filesystem>
#include <ranges>
#include <unordered_map>
#include <algorithm>

namespace vh::fs::model {

enum class PathType {
    FUSE_ROOT,
    VAULT_ROOT,
    CACHE_ROOT,
    THUMBNAIL_ROOT,
    FILE_CACHE_ROOT,
    BACKING_ROOT,
    BACKING_VAULT_ROOT
};

struct Path {
    const std::filesystem::path fuseRoot, vaultRoot, cacheRoot, thumbnailRoot, fileCacheRoot, backingRoot, backingVaultRoot;

    explicit Path(const std::filesystem::path& vaultFuseMount, const std::filesystem::path& vaultBackingMount);

    [[nodiscard]] std::filesystem::path absPath(const std::filesystem::path& relPath, const PathType& type) const;
    [[nodiscard]] std::filesystem::path relPath(const std::filesystem::path& absPath, const PathType& type) const;

    /// Converts an absolute or relative path to an absolute path relative to the root of the specified type.
    [[nodiscard]] std::filesystem::path absRelToRoot(const std::filesystem::path& path, const PathType& type) const;

    /// Converts a path from one type to another, preserving the relative structure.
    /// For example, converting from FUSE_ROOT to VAULT_ROOT.
    /// e.g. absRelToAbsRel("/users/admin/vault1/test.txt", PathType::FUSE_ROOT, PathType::VAULT_ROOT)
    /// return "/vault1/test.txt"
    [[nodiscard]] std::filesystem::path absRelToAbsRel(const std::filesystem::path& path, const PathType& initial, const PathType& target) const;
};

inline std::filesystem::path common_path_prefix(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::filesystem::path result;
    auto ait = a.begin();
    auto bit = b.begin();

    while (ait != a.end() && bit != b.end() && *ait == *bit) {
        result /= *ait;
        ++ait;
        ++bit;
    }

    return result;
}

inline std::string trim(const std::string& str) {
    const auto strBegin = str.find_first_not_of(" \t\n\r");
    if (strBegin == std::string::npos) return ""; // no content

    const auto strEnd = str.find_last_not_of(" \t\n\r");
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

inline std::filesystem::path makeAbsolute(const std::filesystem::path& path) {
    if (path.empty()) return "/";
    if (path.is_absolute()) return path.lexically_normal();
    return std::filesystem::path("/") / path.lexically_normal();
}

inline std::filesystem::path resolveParent(const std::filesystem::path& path) {
    if (path.empty()) return "/";
    auto norm = path.lexically_normal();
    if (norm == "/") return "/";
    return norm.parent_path().empty() ? "/" : norm.parent_path();
}

inline std::filesystem::path stripLeadingSlash(const std::filesystem::path& path) {
    if (path.empty()) return "/";
    auto norm = path.lexically_normal();
    if (norm.empty() || norm == "/") return "/";
    if (norm.string().front() == '/') return { norm.string().substr(1) };
    return norm;
}

inline std::filesystem::path updateSubdirPath(const std::filesystem::path& oldBase, const std::filesystem::path& newBase, const std::filesystem::path& input) {
    // Make sure input starts with oldBase
    const auto inputStr = input.lexically_normal().string();

    if (const auto oldStr   = oldBase.lexically_normal().string(); inputStr.find(oldStr) != 0)
        throw std::invalid_argument("Input path does not start with old base path");

    // Replace the prefix
    const std::filesystem::path relative = std::filesystem::relative(input, oldBase);
    return newBase / relative;
}

inline std::string inferMimeTypeFromPath(const std::filesystem::path& path) {
    static const std::unordered_map<std::string, std::string> mimeMap = {
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},
        {".pdf", "application/pdf"}, {".txt", "text/plain"}, {".html", "text/html"},
    };

    std::string ext = path.extension().string();
    std::ranges::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    const auto it = mimeMap.find(ext);
    return it != mimeMap.end() ? it->second : "application/octet-stream";
}

inline std::string to_snake_case(const std::filesystem::path& path) {
    std::string result = path.string();
    std::ranges::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    std::ranges::replace(result, ' ', '_');
    return result;
}

}
