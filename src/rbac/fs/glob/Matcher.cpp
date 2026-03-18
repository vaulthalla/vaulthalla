#include "rbac/fs/glob/Matcher.hpp"
#include "rbac/fs/glob/Tokenizer.hpp"
#include "rbac/fs/glob/model/Token.hpp"

namespace vh::rbac::fs::glob {

namespace {
    using Token = model::Token;
    using Pattern = model::Pattern;

    [[nodiscard]] bool matchRecursive(
        const std::vector<Token>& tokens,
        const std::string_view path,
        const std::size_t tokenIndex,
        const std::size_t pathIndex
    ) {
        if (tokenIndex == tokens.size() && pathIndex == path.size()) return true;
        if (tokenIndex == tokens.size()) return false;

        const Token& token = tokens[tokenIndex];

        switch (token.type) {
            case Token::Type::Literal: {
                if (pathIndex + token.value.size() > path.size()) return false;
                if (path.substr(pathIndex, token.value.size()) != token.value) return false;
                return matchRecursive(tokens, path, tokenIndex + 1, pathIndex + token.value.size());
            }

            case Token::Type::Slash: {
                if (pathIndex >= path.size() || path[pathIndex] != '/') return false;
                return matchRecursive(tokens, path, tokenIndex + 1, pathIndex + 1);
            }

            case Token::Type::Question: {
                if (pathIndex >= path.size()) return false;
                if (path[pathIndex] == '/') return false;
                return matchRecursive(tokens, path, tokenIndex + 1, pathIndex + 1);
            }

            case Token::Type::Star: {
                std::size_t i = pathIndex;

                if (matchRecursive(tokens, path, tokenIndex + 1, i)) return true;

                while (i < path.size() && path[i] != '/') {
                    ++i;
                    if (matchRecursive(tokens, path, tokenIndex + 1, i)) return true;
                }

                return false;
            }

            case Token::Type::DoubleStar: {
                for (std::size_t i = pathIndex; i <= path.size(); ++i)
                    if (matchRecursive(tokens, path, tokenIndex + 1, i)) return true;

                return false;
            }
        }

        return false;
    }
}

bool Matcher::validatePath(const std::string_view absolutePath) {
    return !absolutePath.empty() && absolutePath.front() == '/';
}

bool Matcher::matches(const model::Pattern& pattern, const std::string& absolutePath) {
    if (!validatePath(absolutePath)) return false;
    return matchRecursive(pattern.tokens, absolutePath, 0, 0);
}

bool Matcher::matches(const std::string& pattern, const std::string& absolutePath) {
    if (!validatePath(absolutePath)) return false;
    return matches(Tokenizer::parse(pattern), absolutePath);
}

}
