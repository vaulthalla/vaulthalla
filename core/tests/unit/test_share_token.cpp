#include "share/Token.hpp"

#include <gtest/gtest.h>

#include <regex>

using namespace vh::share;

class ShareTokenTest : public ::testing::Test {
protected:
    void SetUp() override {
        Token::setPepperForTesting(std::vector<uint8_t>(32, 0x42));
    }

    void TearDown() override {
        Token::clearPepperForTesting();
    }
};

TEST_F(ShareTokenTest, GeneratesPublicShareTokenFormat) {
    const auto token = Token::generate(TokenKind::PublicShare);
    EXPECT_EQ(token.kind, TokenKind::PublicShare);
    EXPECT_EQ(token.secret.size(), 52u);
    EXPECT_FALSE(token.hash.empty());
    EXPECT_TRUE(std::regex_match(token.raw, std::regex("^vhs_[0-9a-f\\-]{36}_[0-9a-z]+$")));
}

TEST_F(ShareTokenTest, GeneratesShareSessionTokenFormat) {
    const auto token = Token::generate(TokenKind::ShareSession);
    EXPECT_EQ(token.kind, TokenKind::ShareSession);
    EXPECT_EQ(token.secret.size(), 52u);
    EXPECT_FALSE(token.hash.empty());
    EXPECT_TRUE(std::regex_match(token.raw, std::regex("^vhss_[0-9a-f\\-]{36}_[0-9a-z]+$")));
}

TEST_F(ShareTokenTest, ParsesValidTokens) {
    const auto generated = Token::generate(TokenKind::PublicShare);
    const auto parsed = Token::parse(generated.raw);
    EXPECT_EQ(parsed.kind, TokenKind::PublicShare);
    EXPECT_EQ(parsed.lookup_id, generated.lookup_id);
    EXPECT_EQ(parsed.secret, generated.secret);
}

TEST_F(ShareTokenTest, RejectsMalformedTokens) {
    EXPECT_THROW(Token::parse(""), std::invalid_argument);
    EXPECT_THROW(Token::parse("vhs_missing"), std::invalid_argument);
    EXPECT_THROW(Token::parse("bad_00000000-0000-4000-8000-000000000000_secret"), std::invalid_argument);
    EXPECT_THROW(Token::parse("vhs_not-a-uuid_secret"), std::invalid_argument);
    EXPECT_THROW(Token::parse("vhs_00000000-0000-4000-8000-000000000000_"), std::invalid_argument);
    EXPECT_THROW(Token::parse("vhs_00000000-0000-4000-8000-000000000000_has space"), std::invalid_argument);
}

TEST_F(ShareTokenTest, HashVerifiesAndRejectsWrongSecret) {
    const auto generated = Token::generate(TokenKind::PublicShare);
    EXPECT_TRUE(Token::verify(generated.hash, TokenKind::PublicShare, generated.secret));
    EXPECT_FALSE(Token::verify(generated.hash, TokenKind::PublicShare, generated.secret + "x"));
}

TEST_F(ShareTokenTest, HashUsesTokenKindDomainSeparation) {
    const std::string secret = "same-secret-material";
    const auto publicHash = Token::hashSecret(TokenKind::PublicShare, secret);
    const auto sessionHash = Token::hashSecret(TokenKind::ShareSession, secret);
    EXPECT_NE(publicHash, sessionHash);
}

TEST_F(ShareTokenTest, RedactionExcludesSecret) {
    const auto generated = Token::generate(TokenKind::ShareSession);
    const auto redacted = Token::redact(generated.raw);
    EXPECT_NE(redacted.find(generated.lookup_id), std::string::npos);
    EXPECT_EQ(redacted.find(generated.secret), std::string::npos);
    EXPECT_EQ(Token::redact("bad-token"), "<redacted-share-token>");
}
