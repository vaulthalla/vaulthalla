#include "rbac/fs/glob/Tokenizer.hpp"
#include "rbac/fs/glob/model/Token.hpp"

#include <stdexcept>
#include <string>

namespace vh::rbac::fs::glob {
    namespace {
        using Token = model::Token;
        using Pattern = model::Pattern;

        [[nodiscard]] bool isSpecial(const char c) {
            return c == '*' || c == '?' || c == '/';
        }

        void pushLiteralIfNeeded(std::string &buffer, std::vector<Token> &tokens) {
            if (buffer.empty()) return;
            tokens.push_back(Token{Token::Type::Literal, buffer});
            buffer.clear();
        }
    }

    void Tokenizer::validate(const std::string_view pattern) {
        if (pattern.empty()) throw std::runtime_error("Glob pattern cannot be empty");
        if (pattern.front() != '/') throw std::runtime_error(
            "Glob pattern must be vault-absolute and begin with '/'");

        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const char c = pattern[i];

            if (c == '\\') throw std::runtime_error(
                "Glob pattern escaping is not supported: found '\\' at index " + std::to_string(i));

            if (c == '*') {
                std::size_t starCount = 1;
                while (i + 1 < pattern.size() && pattern[i + 1] == '*') ++starCount, ++i;

                if (starCount > 2)
                    throw std::runtime_error(
                        "Glob pattern contains invalid star run of length " + std::to_string(starCount) +
                        "; only '*' and '**' are supported");
            }
        }
    }

    bool Tokenizer::isValid(const std::string_view pattern) {
        try {
            validate(pattern);
            return true;
        } catch (...) {
            return false;
        }
    }

    model::Pattern Tokenizer::parse(const std::string_view pattern) {
        validate(pattern);

        Pattern compiled;
        compiled.source = std::string(pattern);

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
