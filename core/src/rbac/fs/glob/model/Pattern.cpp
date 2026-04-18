#include "rbac/fs/glob/model/Pattern.hpp"
#include "rbac/fs/glob/Tokenizer.hpp"

namespace vh::rbac::fs::glob::model {
    Pattern Pattern::make(const std::string_view source) {
        return Tokenizer::parse(source);
    }
}
