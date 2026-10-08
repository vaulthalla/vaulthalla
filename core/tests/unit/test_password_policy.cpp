#include "auth/registration/Validator.hpp"
#include "crypto/password/Strength.hpp"
#include "crypto/util/hash.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

// The dictionary check matched any word of three or more letters anywhere in the password, so long random passwords
// (a 1Password 64-character one contains "doc", "bet", "sos", ...) were refused more often the longer they were.
// It now refuses a password only when the password itself is a dictionary word.
namespace vh::auth::registration::test_password_policy {

using crypto::password::Strength;

class PasswordPolicy : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        // Every three-letter lowercase word: under the old substring match, no password with three consecutive
        // letters could pass.
        std::ostringstream words;
        for (char a = 'a'; a <= 'z'; ++a)
            for (char b = 'a'; b <= 'z'; ++b)
                for (char c = 'a'; c <= 'z'; ++c) words << a << b << c << '\n';
        words << "sunshine\ncorrect\nhorse\nbattery\nstaple\ndocument\n";
        std::istringstream dictionary(words.str());
        Strength::loadDictionary(dictionary);

        std::istringstream weak("qwerty123456!\n");
        Strength::loadCommonWeakPasswords(weak);
    }

    static bool accepted(const std::string& password) {
        return !Validator::localPasswordPolicyViolation(password).has_value();
    }
};

TEST_F(PasswordPolicy, LongRandomPasswordsContainingWordsAreAccepted) {
    EXPECT_TRUE(accepted("Xq7#docK9!bet$Lm2sosPz8@vR4wT6^yN1uE3iO5aS0dF7gH9jK2lZ4xC6vB8nM1"));
    EXPECT_TRUE(accepted("hT3$documentRw9!pL2@xQ7kZ"));
    for (int i = 0; i < 200; ++i) {
        const auto password = crypto::hash::generate_secure_password(64);
        EXPECT_TRUE(accepted(password)) << password << ": " << *Validator::localPasswordPolicyViolation(password);
    }
}

TEST_F(PasswordPolicy, PassphrasesWithoutDigitsAreAccepted) {
    EXPECT_TRUE(accepted("correct-horse-battery-staple"));
}

TEST_F(PasswordPolicy, DictionaryWordsWithDecorationAreRefused) {
    EXPECT_TRUE(Strength::isDictionaryWord("Sunshine2024!"));
    EXPECT_TRUE(Strength::isDictionaryWord("!!DOCUMENT99"));
    EXPECT_FALSE(accepted("Sunshine2024!"));
    EXPECT_FALSE(Strength::isDictionaryWord("Sun2shine"));
    EXPECT_FALSE(Strength::isDictionaryWord("12345678"));
}

TEST_F(PasswordPolicy, ShortAndWeakPasswordsAreRefused) {
    EXPECT_FALSE(accepted("Ab1!"));                       // too short
    EXPECT_FALSE(accepted("Abcdefgh!xyz"));               // short, no digit
    EXPECT_FALSE(accepted("aaaaaaaaaaaaaaaaaaaaaaaaa"));  // long but one character class
    EXPECT_FALSE(accepted("Qwerty123456!"));              // common password list
    EXPECT_FALSE(accepted(std::string(129, 'a') + "B1!"));
}

TEST_F(PasswordPolicy, RefusalsSayWhy) {
    const auto violation = Validator::localPasswordPolicyViolation("Sunshine2024!");
    ASSERT_TRUE(violation.has_value());
    EXPECT_NE(violation->find("dictionary word"), std::string::npos);
}

}
