#include <gtest/gtest.h>

#include "rbac/fs/glob/Matcher.hpp"
#include "rbac/fs/glob/Tokenizer.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vh::rbac::fs::glob::test {
    using Matcher = vh::rbac::fs::glob::Matcher;
    using Tokenizer = vh::rbac::fs::glob::Tokenizer;
    using Pattern = vh::rbac::fs::glob::model::Pattern;
    using Token = vh::rbac::fs::glob::model::Token;
    using TokenType = vh::rbac::fs::glob::model::Token::Type;

    class GlobPathMatchingTest : public ::testing::Test {
    protected:
        static std::filesystem::path p(const std::string &s) { return std::filesystem::path{s}; }

        static void expectMatch(const std::string &pattern, const std::string &path) {
            EXPECT_TRUE(Matcher::matches(pattern, p(path)))
                << "Expected pattern '" << pattern << "' to match path '" << path << "'";
        }

        static void expectNoMatch(const std::string &pattern, const std::string &path) {
            EXPECT_FALSE(Matcher::matches(pattern, p(path)))
                << "Expected pattern '" << pattern << "' NOT to match path '" << path << "'";
        }

        static void expectTokens(
            const Pattern &pattern,
            const std::vector<std::pair<TokenType, std::string> > &expected
        ) {
            ASSERT_EQ(pattern.tokens.size(), expected.size())
                << "Token count mismatch for pattern: " << pattern.source;

            for (size_t i = 0; i < expected.size(); ++i) {
                EXPECT_EQ(pattern.tokens[i].type, expected[i].first)
                    << "Token type mismatch at index " << i << " for pattern: " << pattern.source;

                EXPECT_EQ(pattern.tokens[i].value, expected[i].second)
                    << "Token value mismatch at index " << i << " for pattern: " << pattern.source;
            }
        }
    };

    TEST_F(GlobPathMatchingTest, Tokenizer_ParsesExactLiteralPath) {
        const auto pattern = Tokenizer::parse("/docs/report.pdf");

        EXPECT_EQ(pattern.source, "/docs/report.pdf");
        expectTokens(pattern, {
                         {TokenType::Slash, "/"},
                         {TokenType::Literal, "docs"},
                         {TokenType::Slash, "/"},
                         {TokenType::Literal, "report.pdf"},
                     });
    }

    TEST_F(GlobPathMatchingTest, Tokenizer_ParsesSingleStarPattern) {
        const auto pattern = Tokenizer::parse("/docs/*.pdf");

        EXPECT_EQ(pattern.source, "/docs/*.pdf");
        expectTokens(pattern, {
                         {TokenType::Slash, "/"},
                         {TokenType::Literal, "docs"},
                         {TokenType::Slash, "/"},
                         {TokenType::Star, "*"},
                         {TokenType::Literal, ".pdf"},
                     });
    }

    TEST_F(GlobPathMatchingTest, Tokenizer_ParsesDoubleStarPattern) {
        const auto pattern = Tokenizer::parse("/docs/**/*.pdf");

        EXPECT_EQ(pattern.source, "/docs/**/*.pdf");
        expectTokens(pattern, {
                         {TokenType::Slash, "/"},
                         {TokenType::Literal, "docs"},
                         {TokenType::Slash, "/"},
                         {TokenType::DoubleStar, "**"},
                         {TokenType::Slash, "/"},
                         {TokenType::Star, "*"},
                         {TokenType::Literal, ".pdf"},
                     });
    }

    TEST_F(GlobPathMatchingTest, Tokenizer_ParsesQuestionPattern) {
        const auto pattern = Tokenizer::parse("/docs/file?.txt");

        EXPECT_EQ(pattern.source, "/docs/file?.txt");
        expectTokens(pattern, {
                         {TokenType::Slash, "/"},
                         {TokenType::Literal, "docs"},
                         {TokenType::Slash, "/"},
                         {TokenType::Literal, "file"},
                         {TokenType::Question, "?"},
                         {TokenType::Literal, ".txt"},
                     });
    }

    TEST_F(GlobPathMatchingTest, Tokenizer_IsValidForBasicPatterns) {
        EXPECT_TRUE(Tokenizer::isValid("/"));
        EXPECT_TRUE(Tokenizer::isValid("/docs/report.pdf"));
        EXPECT_TRUE(Tokenizer::isValid("/docs/*.pdf"));
        EXPECT_TRUE(Tokenizer::isValid("/docs/**"));
        EXPECT_TRUE(Tokenizer::isValid("/docs/file?.txt"));
    }

    TEST_F(GlobPathMatchingTest, Matcher_ExactLiteralMatch) {
        expectMatch("/docs/report.pdf", "/docs/report.pdf");
        expectNoMatch("/docs/report.pdf", "/docs/other.pdf");
        expectNoMatch("/docs/report.pdf", "/docs/report.PDF");
        expectNoMatch("/docs/report.pdf", "/docs/report.pdf.bak");
    }

    TEST_F(GlobPathMatchingTest, Matcher_RootPath) {
        expectMatch("/", "/");
        expectNoMatch("/", "/docs");
        expectNoMatch("/", "/docs/report.pdf");
    }

    TEST_F(GlobPathMatchingTest, Matcher_SingleStarMatchesSingleSegmentOnly) {
        expectMatch("/docs/*.pdf", "/docs/report.pdf");
        expectMatch("/docs/*.pdf", "/docs/a.pdf");
        expectMatch("/docs/*.pdf", "/docs/2026-q1.pdf");

        expectNoMatch("/docs/*.pdf", "/docs/nested/report.pdf");
        expectNoMatch("/docs/*.pdf", "/docs/report.txt");
        expectNoMatch("/docs/*.pdf", "/other/report.pdf");
    }

    TEST_F(GlobPathMatchingTest, Matcher_QuestionMatchesExactlyOneCharacter) {
        expectMatch("/docs/file?.txt", "/docs/file1.txt");
        expectMatch("/docs/file?.txt", "/docs/fileA.txt");
        expectMatch("/docs/file?.txt", "/docs/file_.txt");

        expectNoMatch("/docs/file?.txt", "/docs/file10.txt");
        expectNoMatch("/docs/file?.txt", "/docs/file.txt");
        expectNoMatch("/docs/file?.txt", "/docs/file/1.txt");
    }

    TEST_F(GlobPathMatchingTest, Matcher_DoubleStarMatchesAcrossDirectories) {
        expectMatch("/docs/**/*.pdf", "/docs/reports/annual.pdf");
        expectMatch("/docs/**/*.pdf", "/docs/2026/q1/annual.pdf");
        expectMatch("/docs/**/*.pdf", "/docs/a/b/c/d.pdf");

        expectNoMatch("/docs/**/*.pdf", "/other/reports/annual.pdf");
        expectNoMatch("/docs/**/*.pdf", "/docs/reports/annual.txt");
    }

    TEST_F(GlobPathMatchingTest, Matcher_DoubleStarAtBeginningMatchesAnywhereBelowRoot) {
        expectMatch("/**/*.pdf", "/report.pdf");
        expectMatch("/**/*.pdf", "/docs/report.pdf");
        expectMatch("/**/*.pdf", "/docs/2026/q1/report.pdf");

        expectNoMatch("/**/*.pdf", "/docs/2026/q1/report.txt");
    }

    TEST_F(GlobPathMatchingTest, Matcher_MixedRecursiveAndSegmentWildcards) {
        expectMatch("/images/**/thumb-*.jpg", "/images/raw/thumb-cat.jpg");
        expectMatch("/images/**/thumb-*.jpg", "/images/a/b/c/thumb-dog.jpg");

        expectNoMatch("/images/**/thumb-*.jpg", "/images/a/b/c/full-dog.jpg");
        expectNoMatch("/images/**/thumb-*.jpg", "/videos/a/b/c/thumb-dog.jpg");
        expectNoMatch("/images/**/thumb-*.jpg", "/images/a/b/c/thumb-dog.png");
    }

    TEST_F(GlobPathMatchingTest, Matcher_PrecompiledPatternAndInlinePatternAgree) {
        const auto compiled = Tokenizer::parse("/docs/**/*.txt");

        const std::vector<std::string> matchingPaths{
            "/docs/a.txt",
            "/docs/notes/a.txt",
            "/docs/2026/q1/summary.txt",
        };

        const std::vector<std::string> nonMatchingPaths{
            "/doc/a.txt",
            "/docs/a.pdf",
            "/tmp/docs/a.txt",
        };

        for (const auto &path: matchingPaths) {
            EXPECT_TRUE(Matcher::matches(compiled, p(path))) << path;
            EXPECT_TRUE(Matcher::matches("/docs/**/*.txt", p(path))) << path;
        }

        for (const auto &path: nonMatchingPaths) {
            EXPECT_FALSE(Matcher::matches(compiled, p(path))) << path;
            EXPECT_FALSE(Matcher::matches("/docs/**/*.txt", p(path))) << path;
        }
    }

    TEST_F(GlobPathMatchingTest, Matcher_RejectsRelativePaths) {
        expectNoMatch("/docs/*.pdf", "docs/report.pdf");
        expectNoMatch("/**/*.txt", "notes/todo.txt");
        expectNoMatch("/", ".");
    }

    TEST_F(GlobPathMatchingTest, Matcher_NormalizesDotSegmentsIfSupportedByImplementation) {
        expectMatch("/docs/report.pdf", "/docs/./report.pdf");
        expectMatch("/docs/**/*.pdf", "/docs/a/../a/report.pdf");
    }

    TEST_F(GlobPathMatchingTest, Matcher_DoesNotAllowSingleStarToCrossSlash) {
        expectNoMatch("/*/report.pdf", "/docs/2026/report.pdf");
        expectMatch("/*/report.pdf", "/docs/report.pdf");
    }

    TEST_F(GlobPathMatchingTest, Matcher_MatchesDirectoryNamesLiterallyWhenNoWildcardsPresent) {
        expectMatch("/images/raw/cat.png", "/images/raw/cat.png");

        expectNoMatch("/images/raw/cat.png", "/images/raw/dog.png");
        expectNoMatch("/images/raw/cat.png", "/images/RAW/cat.png");
        expectNoMatch("/images/raw/cat.png", "/images/raw/archive/cat.png");
    }

    TEST_F(GlobPathMatchingTest, Matcher_CanHandleDeepPaths) {
        expectMatch(
            "/vault/**/final-?.tar.gz",
            "/vault/a/b/c/d/e/f/g/final-1.tar.gz"
        );

        expectMatch(
            "/vault/**/final-?.tar.gz",
            "/vault/releases/final-a.tar.gz"
        );

        expectNoMatch(
            "/vault/**/final-?.tar.gz",
            "/vault/releases/final-12.tar.gz"
        );

        expectNoMatch(
            "/vault/**/final-?.tar.gz",
            "/vault/releases/final-.tar.gz"
        );
    }

    TEST_F(GlobPathMatchingTest, Matcher_EmptyLikeLeafSegmentsBehaveCorrectly) {
        expectNoMatch("/docs/*.pdf", "/docs/.pdf");
        expectNoMatch("/docs/file?.txt", "/docs/file.txt");
        expectMatch("/docs/*", "/docs/report");
        expectNoMatch("/docs/*", "/docs/a/report");
    }

    TEST_F(GlobPathMatchingTest, Matcher_RBAC_Override_AllowsTxtInDocsDirectory) {
        const std::string pattern = "/perm_override_allow_seed/docs/*.txt";
        const std::string path = "/perm_override_allow_seed/docs/secret.txt";

        EXPECT_TRUE(Matcher::matches(pattern, p(path)))
            << "RBAC override glob failed: pattern '" << pattern
            << "' should match path '" << path << "'";
    }

    TEST_F(GlobPathMatchingTest, Matcher_RequiresTraversalThrough_ShallowFileOverride) {
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/")));
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/docs")));

        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/images")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/docs/secret.txt")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/docs/nested")));
    }

    TEST_F(GlobPathMatchingTest, Matcher_RequiresTraversalThrough_DeepRecursiveOverride) {
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/private/**/*.pdf", p("/")));
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/private/**/*.pdf", p("/docs")));
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/private/**/*.pdf", p("/docs/private")));

        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/private/**/*.pdf", p("/tmp")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/private/**/*.pdf", p("/docs/public")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/private/**/*.pdf", p("/docs/private/report.pdf")));
    }

    TEST_F(GlobPathMatchingTest, Matcher_RequiresTraversalThrough_ExactLiteralTargetOnlyUsesAncestors) {
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/report.pdf", p("/")));
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/docs/report.pdf", p("/docs")));

        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/report.pdf", p("/docs/report.pdf")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/report.pdf", p("/images")));
    }

    TEST_F(GlobPathMatchingTest, Matcher_RequiresTraversalThrough_RbacOverrideCorridorCase) {
        EXPECT_TRUE(Matcher::requiresTraversalThrough(
            "/perm_override_allow_seed/docs/*.txt",
            p("/")
        ));

        EXPECT_TRUE(Matcher::requiresTraversalThrough(
            "/perm_override_allow_seed/docs/*.txt",
            p("/perm_override_allow_seed")
        ));

        EXPECT_TRUE(Matcher::requiresTraversalThrough(
            "/perm_override_allow_seed/docs/*.txt",
            p("/perm_override_allow_seed/docs")
        ));

        EXPECT_FALSE(Matcher::requiresTraversalThrough(
            "/perm_override_allow_seed/docs/*.txt",
            p("/perm_override_allow_seed/docs/secret.txt")
        ));

        EXPECT_FALSE(Matcher::requiresTraversalThrough(
            "/perm_override_allow_seed/docs/*.txt",
            p("/perm_override_allow_seed/images")
        ));
    }

    TEST_F(GlobPathMatchingTest, Matcher_RequiresTraversalThrough_DoesNotTreatUnrelatedSiblingAsCorridor) {
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/doc")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/*.txt", p("/docs2")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/docs/private/*.txt", p("/docs/public")));
    }

    TEST_F(GlobPathMatchingTest, Matcher_RequiresTraversalThrough_DoubleStarAtRoot) {
        EXPECT_TRUE(Matcher::requiresTraversalThrough("/**/*.pdf", p("/")));
        EXPECT_FALSE(Matcher::requiresTraversalThrough("/**/*.pdf", p("/report.pdf")));
    }
}
