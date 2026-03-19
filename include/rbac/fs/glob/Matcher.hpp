#pragma once

#include "rbac/fs/glob/model/Pattern.hpp"

#include <filesystem>
#include <string_view>

namespace vh::rbac::fs::glob {
    struct Matcher {
        using Path = std::filesystem::path;

        // Match a precompiled pattern against a vault-absolute path such as:
        //   /docs/report.pdf
        //   /images/raw/cat.png
        static bool matches(const model::Pattern &pattern, const Path &path);

        // Parse + match in one call.
        static bool matches(std::string_view pattern, const Path &path);

    private:
        static bool validatePath(const Path &path);
        static std::string normalizePath(const Path &path);
    };
}
