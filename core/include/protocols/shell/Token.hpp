#pragma once

#include "runtime/Deps.hpp"
#include "UsageManager.hpp"
#include "CommandUsage.hpp"

#include <string>
#include <vector>
#include <string_view>

namespace vh::protocols::shell {

enum class TokenType { Word, Flag /*, Value (unused) */ };

struct Token {
    TokenType type;
    std::string text;
};

inline bool looks_negative_number(std::string_view s) {
    if (s.size() < 2 || s[0] != '-') return false;
    bool dot = false, digit = false;
    for (size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') { digit = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return digit;
}

inline void pushFlag(std::vector<Token>& out, std::string k) {
    // Strip leading '-' if present (callers usually pass already-stripped)
    if (!k.empty() && k[0] == '-') k.erase(k.begin());
    out.push_back({TokenType::Flag, std::move(k)});
}
inline void pushWord(std::vector<Token>& out, std::string v) {
    out.push_back({TokenType::Word, std::move(v)});
}

inline void skip_ws(const char*& p, const char* e) {
    while (p < e && (*p == ' ' || *p == '\t')) ++p;
}

inline std::string read_quoted(const char*& p, const char* e, char quote) {
    // p is at the opening quote; consume it
    ++p;
    std::string buf;
    buf.reserve(32);
    while (p < e) {
        if (*p == quote) { ++p; break; }
        if (quote == '"' && *p == '\\' && p + 1 < e) {
            // In double quotes, honor \" and \\ escapes
            ++p;
            buf.push_back(*p++);
            continue;
        }
        buf.push_back(*p++);
    }
    return buf; // no outer quotes, escapes resolved for double quotes
}

inline std::string read_unquoted_atom(const char*& p, const char* e) {
    // Read until whitespace or quote (we stop before quote to let caller handle it)
    const char* b = p;
    while (p < e && *p != ' ' && *p != '\t' && *p != '"' && *p != '\'') ++p;
    return {b, static_cast<std::string::size_type>(p - b)};
}

// Heuristic: decide if "-XYZ" is a bundle or "-X<value>".
// If tail contains obvious value chars (/, ., :, =), treat as glued value.
inline bool looks_glued_value(std::string_view tail) {
    return std::ranges::any_of(tail, [](char c) { return c == '/' || c == '.' || c == ':' || c == '='; });
}

// Expand short bundle "-abc" -> flags a,b,c
inline void expand_bundle(std::string_view bundle, std::vector<Token>& out) {
    for (const char c : bundle) pushFlag(out, std::string(1, c));
}

inline std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> out;
    out.reserve(16);

    const char* s = line.c_str();
    const char* e = s + line.size();
    const char* p = s;

    // Strip command prefix if present
    // e.g. "vh", "vaulthalla", or any alias
    for (const auto& aliases : runtime::Deps::get().shellUsageManager->root()->aliases) {
        size_t len = aliases.size();
        if (line.size() >= len && line.compare(0, len, aliases) == 0) {
            p += len;
            break;
        }
    }

    skip_ws(p, e);
    while (p < e) {
        // Handle sentinel "--" exactly
        if (*p == '-' && (p + 1) < e && p[1] == '-' && (p + 2 == e || p[2] == ' ' || p[2] == '\t')) {
            p += 2;
            pushWord(out, "--");
            skip_ws(p, e);
            continue;
        }

        if (*p == '"' || *p == '\'') {
            // Bare quoted atom -> Word
            char q = *p;
            auto val = read_quoted(p, e, q);
            pushWord(out, std::move(val));
            skip_ws(p, e);
            continue;
        }

        if (*p == '-') {
            // Long or short flag
            // Peek ahead to distinguish "--" vs "-"* and guard negative numbers.
            // If it's a negative number, treat as Word.
            // (We need the full atom to check; read a provisional unquoted chunk first, but also allow inline quotes after '='.)
            std::string pre = read_unquoted_atom(p, e); // may be "-k", "-abc", "--key", "--key=", "-I/usr"
            // If we stopped due to a quote right after '=', fold quoted value inline
            if (p < e && (*p == '"' || *p == '\'')) {
                // Only makes sense if pre ends with '=' or is "-k" (glued form -k"value")
                if (!pre.empty() && (pre.back() == '=' || (pre.size() == 2 && pre[0] == '-' && pre[1] != '-'))) {
                    char q = *p;
                    auto val = read_quoted(p, e, q);
                    pre += val; // pre now has `--key=val` OR `-kval`
                }
            }

            // Negative number? (e.g., "-1.5")
            if (looks_negative_number(pre)) {
                pushWord(out, std::move(pre));
                skip_ws(p, e);
                continue;
            }

            // Long flag forms: --key or --key=value
            if (pre.rfind("--", 0) == 0) {
                if (out.empty()) {
                    // First token: allow it to be a command
                    pushWord(out, std::move(pre));
                } else {
                    auto eq = pre.find('=');
                    if (eq == std::string::npos) {
                        pushFlag(out, pre.substr(2));
                    } else {
                        pushFlag(out, pre.substr(2, eq - 2));
                        pushWord(out, pre.substr(eq + 1));
                    }
                }
                skip_ws(p, e);
                continue;
            }

            // Short flag(s)
            // pre starts with '-' and not '--'
            if (pre.size() >= 2 && pre[0] == '-' && pre[1] != '-') {
                if (pre.size() == 2) {
                    if (out.empty()) pushWord(out, pre); // First token: treat as command alias
                    else pushFlag(out, pre.substr(1));
                } else {
                    // Could be bundle "-abc" or glued "-kVALUE"
                    std::string_view tail = std::string_view(pre).substr(2);
                    if (looks_glued_value(tail)) {
                        // Emit "-k" + value (strip any '=' if user wrote -k=value)
                        pushFlag(out, std::string(1, pre[1]));
                        std::string value = std::string(tail);
                        if (!value.empty() && value[0] == '=') value.erase(value.begin());
                        pushWord(out, std::move(value));
                    } else {
                        // Pure bundle
                        expand_bundle(tail, out); // emits a,b,c…
                    }
                }
                skip_ws(p, e);
                continue;
            }

            // Fallback (shouldn’t hit): treat as word
            pushWord(out, std::move(pre));
            skip_ws(p, e);
            continue;
        }

        // Unquoted atom (could be plain word or key=value with no leading dashes)
        std::string atom = read_unquoted_atom(p, e);
        // If we stopped on a quote, fold it immediately (handles foo="bar baz")
        if (p < e && (*p == '"' || *p == '\'')) {
            if (!atom.empty() && atom.back() == '=') {
                char q = *p;
                auto val = read_quoted(p, e, q);
                atom += val;
            }
        }
        // No leading dashes here: always a Word
        pushWord(out, std::move(atom));
        skip_ws(p, e);
    }

    return out;
}

inline std::string to_string(const Token& t) {
    switch (t.type) {
    case TokenType::Word: return "Word(" + t.text + ")";
    case TokenType::Flag: return "Flag(" + t.text + ")";
        // case TokenType::Value: return "Value(" + t.text + ")"; // unused
    }
    return "UnknownToken";
}

inline std::string to_string(const std::vector<Token>& tokens) {
    std::string out;
    out.reserve(64 + tokens.size() * 16);
    for (const auto& t : tokens) {
        if (!out.empty()) out += " ";
        out += to_string(t);
    }
    return out;
}

}
