#pragma once

#include "rbac/fs/glob/model/Pattern.hpp"

#include <string_view>

namespace vh::rbac::fs::glob {
    struct Tokenizer {
        static model::Pattern parse(std::string_view pattern);
        static void validate(std::string_view pattern);
    };
}
