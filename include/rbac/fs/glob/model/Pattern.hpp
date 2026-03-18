#pragma once

#include "Token.hpp"

#include <string>
#include <vector>

namespace vh::rbac::fs::glob::model {

struct Pattern {
    std::string source{};
    std::vector<Token> tokens{};
};

}
