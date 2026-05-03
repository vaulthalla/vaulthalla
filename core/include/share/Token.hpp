#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::share {

enum class TokenKind { PublicShare, ShareSession };

struct ParsedToken {
    TokenKind kind{};
    std::string lookup_id;
    std::string secret;
};

struct GeneratedToken {
    TokenKind kind{};
    std::string lookup_id;
    std::string secret;
    std::string raw;
    std::vector<uint8_t> hash;
};

class Token {
  public:
    static GeneratedToken generate(TokenKind kind);
    static ParsedToken parse(std::string_view raw);
    static std::vector<uint8_t> hashSecret(TokenKind kind, std::string_view secret);
    static std::vector<uint8_t> hashForDomain(std::string_view domain, std::string_view value);
    static bool verify(const std::vector<uint8_t>& expected_hash, TokenKind kind, std::string_view secret);
    static std::string redact(std::string_view raw_or_lookup);

    static void setPepperForTesting(std::vector<uint8_t> pepper);
    static void clearPepperForTesting();
};

[[nodiscard]] std::string to_string(TokenKind kind);

}
