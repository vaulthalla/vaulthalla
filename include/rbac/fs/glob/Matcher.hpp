#pragma once

#include "rbac/fs/glob/model/Pattern.hpp"

#include <string>
#include <string_view>

namespace vh::rbac::fs::glob {

struct Matcher {
    static bool matches(const model::Pattern& pattern, const std::string& absolutePath);
    static bool matches(const std::string& pattern, const std::string& absolutePath);

private:
    static bool validatePath(std::string_view absolutePath);
};

}
