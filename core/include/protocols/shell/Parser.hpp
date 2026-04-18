#pragma once

#include "protocols/shell/Token.hpp"
#include "protocols/shell/types.hpp"

#include <string>
#include <vector>
#include <optional>
#include <cctype>

namespace vh::protocols::shell {

// Upsert a flag (last wins)
inline void setOpt(CommandCall& c,
                   const std::string& key,
                   const std::optional<std::string>& val) {
    for (auto& [k, v] : c.options) if (k == key) { v = val; return; }
    c.options.push_back(FlagKV{key, val});
}

static inline bool looks_number(std::string s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '-') { if (s.size()==1) return false; ++i; }
    bool dot=false, digit=false;
    for (; i<s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (std::isdigit(c)) { digit=true; continue; }
        if (s[i]=='.' && !dot) { dot=true; continue; }
        return false;
    }
    return digit;
}

inline CommandCall parseTokens(const std::vector<Token>& toks) {
    CommandCall call;
    call.options.reserve(8);
    call.positionals.reserve(8);

    size_t i = 0;

    // 1) Command name = first Word
    for (; i < toks.size(); ++i) {
        if (toks[i].type == TokenType::Word) {
            call.name = toks[i].text;
            ++i;
            break;
        }
    }
    // Optional: allow leading Flag as "name" (help-style)
    if (call.name.empty() && !toks.empty() && toks[0].type == TokenType::Flag) {
        call.name = toks[0].text;
        i = 1;
    }

    bool stop_flags = false;

    for (; i < toks.size(); ++i) {
        const Token& t = toks[i];

        // Sentinel: "--" arrives as a Word from the tokenizer
        if (!stop_flags && t.type == TokenType::Word && t.text == "--") {
            stop_flags = true;
            continue;
        }

        if (!stop_flags && t.type == TokenType::Flag) {
            const auto key = t.text;
            if (i + 1 < toks.size() && toks[i+1].type == TokenType::Word) {
                setOpt(call, key, toks[i+1].text);
                ++i; // consumed value
            } else {
                setOpt(call, key, std::nullopt);
            }
            continue;
        }

        // Positional (either after "--" or just a Word)
        call.positionals.push_back(t.text);
    }

    return call;
}

}
