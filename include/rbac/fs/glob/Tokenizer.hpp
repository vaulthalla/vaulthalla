#pragma once

#include "rbac/fs/glob/model/Pattern.hpp"

#include <string>

namespace vh::rbac::fs::glob {

struct Tokenizer {
    static model::Pattern parse(const std::string& pattern);
    static void validate(const std::string& pattern);
};

}
