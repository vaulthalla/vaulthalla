#include "share/AuditEvent.hpp"
#include "share/EmailChallenge.hpp"
#include "share/Grant.hpp"
#include "share/Link.hpp"
#include "share/Session.hpp"
#include "share/Token.hpp"
#include "share/Upload.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace vh::share;

class ShareDomainTest : public ::testing::Test {
protected:
    void SetUp() override {
        Token::setPepperForTesting(std::vector<uint8_t>(32, 0x24));
    }

    void TearDown() override {
        Token::clearPepperForTesting();
    }
};

TEST_F(ShareDomainTest, EnumStringRoundTrips) {
    EXPECT_EQ(operation_from_string(to_string(Operation::Download)), Operation::Download);
    EXPECT_EQ(target_type_from_string(to_string(TargetType::Directory)), TargetType::Directory);
    EXPECT_EQ(link_type_from_string(to_string(LinkType::Access)), LinkType::Access);
    EXPECT_EQ(access_mode_from_string(to_string(AccessMode::EmailValidated)), AccessMode::EmailValidated);
    EXPECT_EQ(duplicate_policy_from_string(to_string(DuplicatePolicy::Rename)), DuplicatePolicy::Rename);
    EXPECT_EQ(upload_status_from_string(to_string(UploadStatus::Receiving)), UploadStatus::Receiving);
    EXPECT_EQ(audit_actor_type_from_string(to_string(AuditActorType::SharePrincipal)), AuditActorType::SharePrincipal);
    EXPECT_EQ(audit_status_from_string(to_string(AuditStatus::RateLimited)), AuditStatus::RateLimited);
    EXPECT_THROW({ (void)link_type_from_string("bogus"); }, std::invalid_argument);
}

TEST_F(ShareDomainTest, GrantBitmaskAndValidation) {
    Grant grant;
    grant.vault_id = 1;
    grant.root_entry_id = 2;
    grant.root_path = "/";
    grant.target_type = TargetType::Directory;
    grant.allowed_ops = bit(Operation::Metadata) | bit(Operation::Upload);

    EXPECT_TRUE(grant.allows(Operation::Metadata));
    EXPECT_TRUE(grant.allows(Operation::Upload));
    EXPECT_FALSE(grant.allows(Operation::Download));
    EXPECT_NO_THROW(grant.requireValid());

    grant.target_type = TargetType::File;
    grant.allowed_ops |= bit(Operation::Mkdir);
    EXPECT_THROW(grant.requireValid(), std::invalid_argument);
}

TEST_F(ShareDomainTest, LinkStateHelpersAndSerializationRedactSensitiveFields) {
    Link link;
    link.id = "00000000-0000-4000-8000-000000000001";
    link.token_lookup_id = "00000000-0000-4000-8000-000000000002";
    link.token_hash = Token::hashSecret(TokenKind::PublicShare, "secret");
    link.created_by = 1;
    link.vault_id = 2;
    link.root_entry_id = 3;
    link.root_path = "/";
    link.target_type = TargetType::Directory;
    link.link_type = LinkType::Access;
    link.access_mode = AccessMode::EmailValidated;
    link.allowed_ops = bit(Operation::Metadata);
    link.created_at = std::time(nullptr);
    link.updated_at = link.created_at;
    link.expires_at = link.created_at + 60;

    EXPECT_TRUE(link.isActive(link.created_at));
    EXPECT_TRUE(link.requiresEmail());
    EXPECT_TRUE(link.isExpired(link.created_at + 61));

    nlohmann::json management = link;
    EXPECT_FALSE(management.contains("token_hash"));
    EXPECT_FALSE(management.dump().contains("secret"));

    const auto publicJson = link.toPublicJson();
    EXPECT_FALSE(publicJson.contains("token_lookup_id"));
    EXPECT_FALSE(publicJson.contains("created_by"));
}

TEST_F(ShareDomainTest, SessionChallengeAndUploadStateHelpers) {
    const auto now = std::time(nullptr);

    Session session;
    session.expires_at = now + 60;
    EXPECT_TRUE(session.isActive(now));
    EXPECT_FALSE(session.isVerified());
    session.verified_at = now;
    EXPECT_TRUE(session.isVerified());
    session.revoked_at = now;
    EXPECT_FALSE(session.isActive(now));

    const auto email = EmailChallenge::normalizeEmail(" Test@Example.COM ");
    EXPECT_EQ(email, "test@example.com");
    const auto code = EmailChallenge::generateCode();
    const auto hash = EmailChallenge::hashCode(code);
    EXPECT_TRUE(EmailChallenge::verifyCode(hash, code));
    EXPECT_FALSE(EmailChallenge::verifyCode(hash, "000000"));

    EmailChallenge challenge;
    challenge.expires_at = now + 60;
    challenge.max_attempts = 2;
    challenge.attempts = 1;
    EXPECT_TRUE(challenge.canAttempt(now));
    challenge.attempts = 2;
    EXPECT_FALSE(challenge.canAttempt(now));

    Upload upload;
    upload.expected_size_bytes = 10;
    upload.received_size_bytes = 4;
    EXPECT_FALSE(upload.exceedsExpectedSize(6));
    EXPECT_TRUE(upload.exceedsExpectedSize(7));
    upload.status = UploadStatus::Complete;
    EXPECT_TRUE(upload.isTerminal());
}

TEST_F(ShareDomainTest, AuditSerializationIsRedactedShape) {
    AuditEvent event;
    event.id = 1;
    event.actor_type = AuditActorType::OwnerUser;
    event.event_type = "share.create";
    event.status = AuditStatus::Success;
    event.created_at = std::time(nullptr);
    event.error_message = "redacted failure";

    nlohmann::json json = event;
    EXPECT_FALSE(json.contains("token_hash"));
    EXPECT_FALSE(json.dump().contains("vhs_"));
}
