#include "rbac/fs/glob/Tokenizer.hpp"
#include "rbac/fs/glob/model/Token.hpp"

#include <stdexcept>

namespace vh::rbac::fs::glob {

namespace {
    using Token = model::Token;
    using Pattern = model::Pattern;

    [[nodiscard]] bool isSpecial(const char c) {
        return c == '*' || c == '?' || c == '/';
    }

    // [[nodiscard]] std::string tokenTypeToString(const Token::Type type) {
    //     switch (type) {
    //         case Token::Type::Literal: return "Literal";
    //         case Token::Type::Star: return "Star";
    //         case Token::Type::DoubleStar: return "DoubleStar";
    //         case Token::Type::Question: return "Question";
    //         case Token::Type::Slash: return "Slash";
    //     }
    //
    //     return "Unknown";
    // }

    void pushLiteralIfNeeded(std::string& buffer, std::vector<Token>& tokens) {
        if (buffer.empty()) return;
        tokens.push_back(Token{Token::Type::Literal, buffer});
        buffer.clear();
    }
}

void Tokenizer::validate(const std::string& pattern) {
    if (pattern.empty()) throw std::runtime_error("Glob pattern cannot be empty");
    if (pattern.front() != '/') throw std::runtime_error("Glob pattern must be absolute relative to vault and begin with '/'");

    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];

        if (c == '\\') throw std::runtime_error("Glob pattern escaping is not supported: found '\\' at index " + std::to_string(i));

        if (c == '*') {
            std::size_t starCount = 1;
            while (i + 1 < pattern.size() && pattern[i + 1] == '*') ++starCount, ++i;

            if (starCount > 2)
                throw std::runtime_error("Glob pattern contains invalid star run of length " + std::to_string(starCount) + "; only '*' and '**' are supported");
        }
    }
}

model::Pattern Tokenizer::parse(const std::string& pattern) {
    validate(pattern);

    Pattern compiled;
    compiled.source = pattern;

    std::string literalBuffer;

    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];

        if (!isSpecial(c)) {
            literalBuffer.push_back(c);
            continue;
        }

        pushLiteralIfNeeded(literalBuffer, compiled.tokens);

        if (c == '/') {
            compiled.tokens.push_back(Token{Token::Type::Slash, "/"});
            continue;
        }

        if (c == '?') {
            compiled.tokens.push_back(Token{Token::Type::Question, "?"});
            continue;
        }

        if (c == '*') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
                compiled.tokens.push_back(Token{Token::Type::DoubleStar, "**"});
                ++i;
            } else {
                compiled.tokens.push_back(Token{Token::Type::Star, "*"});
            }

            continue;
        }
    }

    pushLiteralIfNeeded(literalBuffer, compiled.tokens);

    return compiled;
}

}
