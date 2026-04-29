#include "identities/User.hpp"
#include "rbac/Actor.hpp"
#include "share/AuditEvent.hpp"
#include "share/EmailChallenge.hpp"
#include "share/Manager.hpp"
#include "share/Token.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <stdexcept>
#include <unordered_map>

using namespace vh;
using namespace vh::share;

namespace vh::share::test_manager {
constexpr std::time_t kManagerNow = 1'800'000'000;

std::string uuidFor(const uint32_t value) {
    return std::format("00000000-0000-4000-8000-{:012d}", value);
}

class FakeAuthorizer final : public ShareAuthorizer {
public:
    bool allow_create{true};
    bool allow_update{true};
    bool allow_manage{true};
    bool allow_list_vault{true};

    AuthorizationDecision canCreateLink(const rbac::Actor& actor, const Link&) const override {
        if (!actor.isHuman()) return {false, "not_human"};
        return {allow_create, allow_create ? "allowed" : "create_denied"};
    }

    AuthorizationDecision canUpdateLink(const rbac::Actor& actor, const Link&, const Link&) const override {
        if (!actor.isHuman()) return {false, "not_human"};
        return {allow_update, allow_update ? "allowed" : "update_denied"};
    }

    AuthorizationDecision canManageLink(const rbac::Actor& actor, const Link&) const override {
        if (!actor.isHuman()) return {false, "not_human"};
        return {allow_manage, allow_manage ? "allowed" : "manage_denied"};
    }

    AuthorizationDecision canListVaultLinks(const rbac::Actor& actor, uint32_t) const override {
        if (!actor.isHuman()) return {false, "not_human"};
        return {allow_list_vault, allow_list_vault ? "allowed" : "list_denied"};
    }
};

class FakeStore final : public ShareStore {
public:
    std::unordered_map<std::string, std::shared_ptr<Link>> links;
    std::unordered_map<std::string, std::string> link_by_lookup;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
    std::unordered_map<std::string, std::string> session_by_lookup;
    std::unordered_map<std::string, std::shared_ptr<EmailChallenge>> challenges;
    std::vector<std::shared_ptr<AuditEvent>> audits;

    uint32_t next_link{100};
    uint32_t next_session{200};
    uint32_t next_challenge{300};

    std::shared_ptr<Link> createLink(const std::shared_ptr<Link>& link) override {
        auto stored = std::make_shared<Link>(*link);
        if (stored->id.empty()) stored->id = uuidFor(next_link++);
        stored->created_at = kManagerNow;
        stored->updated_at = kManagerNow;
        links[stored->id] = stored;
        link_by_lookup[stored->token_lookup_id] = stored->id;
        return stored;
    }

    std::shared_ptr<Link> getLink(const std::string& id) override {
        if (!links.contains(id)) return nullptr;
        return links.at(id);
    }

    std::shared_ptr<Link> getLinkByLookupId(const std::string& lookupId) override {
        if (!link_by_lookup.contains(lookupId)) return nullptr;
        return getLink(link_by_lookup.at(lookupId));
    }

    std::vector<std::shared_ptr<Link>> listLinksForUser(uint32_t userId, const db::model::ListQueryParams&) override {
        std::vector<std::shared_ptr<Link>> out;
        for (auto& [_, link] : links)
            if (link->created_by == userId) out.push_back(link);
        return out;
    }

    std::vector<std::shared_ptr<Link>> listLinksForVault(uint32_t vaultId, const db::model::ListQueryParams&) override {
        std::vector<std::shared_ptr<Link>> out;
        for (auto& [_, link] : links)
            if (link->vault_id == vaultId) out.push_back(link);
        return out;
    }

    std::shared_ptr<Link> updateLink(const std::shared_ptr<Link>& link) override {
        auto stored = std::make_shared<Link>(*link);
        stored->updated_at = kManagerNow + 1;
        links[stored->id] = stored;
        link_by_lookup[stored->token_lookup_id] = stored->id;
        return stored;
    }

    void revokeLink(const std::string& id, uint32_t revokedBy) override {
        auto link = getLink(id);
        if (!link) throw std::runtime_error("missing link");
        link->revoked_at = kManagerNow;
        link->revoked_by = revokedBy;
    }

    void rotateLinkToken(
        const std::string& id,
        const std::string& lookupId,
        const std::vector<uint8_t>& tokenHash,
        uint32_t updatedBy
    ) override {
        auto link = getLink(id);
        if (!link) throw std::runtime_error("missing link");
        link_by_lookup.erase(link->token_lookup_id);
        link->token_lookup_id = lookupId;
        link->token_hash = tokenHash;
        link->updated_by = updatedBy;
        link_by_lookup[lookupId] = id;
    }

    void touchLinkAccess(const std::string& id) override {
        auto link = getLink(id);
        if (!link) throw std::runtime_error("missing link");
        link->last_accessed_at = kManagerNow;
        ++link->access_count;
    }

    void incrementDownload(const std::string& id) override {
        auto link = getLink(id);
        if (!link) throw std::runtime_error("missing link");
        ++link->download_count;
    }

    std::shared_ptr<Session> createSession(const std::shared_ptr<Session>& session) override {
        auto stored = std::make_shared<Session>(*session);
        if (stored->id.empty()) stored->id = uuidFor(next_session++);
        stored->created_at = kManagerNow;
        stored->last_seen_at = kManagerNow;
        sessions[stored->id] = stored;
        session_by_lookup[stored->session_token_lookup_id] = stored->id;
        return stored;
    }

    std::shared_ptr<Session> getSession(const std::string& id) override {
        if (!sessions.contains(id)) return nullptr;
        return sessions.at(id);
    }

    std::shared_ptr<Session> getSessionByLookupId(const std::string& lookupId) override {
        if (!session_by_lookup.contains(lookupId)) return nullptr;
        return getSession(session_by_lookup.at(lookupId));
    }

    void verifySession(const std::string& sessionId, const std::vector<uint8_t>& emailHash) override {
        auto session = getSession(sessionId);
        if (!session) throw std::runtime_error("missing session");
        session->email_hash = emailHash;
        session->verified_at = kManagerNow;
    }

    void touchSession(const std::string& sessionId) override {
        auto session = getSession(sessionId);
        if (!session) throw std::runtime_error("missing session");
        session->last_seen_at = kManagerNow + 1;
    }

    void revokeSession(const std::string& sessionId) override {
        auto session = getSession(sessionId);
        if (!session) throw std::runtime_error("missing session");
        session->revoked_at = kManagerNow;
    }

    void revokeSessionsForShare(const std::string& shareId) override {
        for (auto& [_, session] : sessions)
            if (session->share_id == shareId && !session->revoked_at) session->revoked_at = kManagerNow;
    }

    std::shared_ptr<EmailChallenge> createEmailChallenge(const std::shared_ptr<EmailChallenge>& challenge) override {
        auto stored = std::make_shared<EmailChallenge>(*challenge);
        if (stored->id.empty()) stored->id = uuidFor(next_challenge++);
        stored->created_at = kManagerNow;
        challenges[stored->id] = stored;
        return stored;
    }

    std::shared_ptr<EmailChallenge> getEmailChallenge(const std::string& id) override {
        if (!challenges.contains(id)) return nullptr;
        return challenges.at(id);
    }

    std::shared_ptr<EmailChallenge> getActiveEmailChallenge(
        const std::string& shareId,
        const std::vector<uint8_t>& emailHash
    ) override {
        std::shared_ptr<EmailChallenge> best;
        for (auto& [_, challenge] : challenges) {
            if (challenge->share_id != shareId) continue;
            if (challenge->email_hash != emailHash) continue;
            if (!challenge->canAttempt(kManagerNow)) continue;
            if (!best || challenge->created_at > best->created_at) best = challenge;
        }
        return best;
    }

    void recordEmailChallengeAttempt(const std::string& challengeId) override {
        auto challenge = getEmailChallenge(challengeId);
        if (!challenge) throw std::runtime_error("missing challenge");
        ++challenge->attempts;
    }

    void consumeEmailChallenge(const std::string& challengeId) override {
        auto challenge = getEmailChallenge(challengeId);
        if (!challenge) throw std::runtime_error("missing challenge");
        if (challenge->consumed_at) throw std::runtime_error("challenge consumed");
        challenge->consumed_at = kManagerNow;
    }

    void appendAuditEvent(const std::shared_ptr<AuditEvent>& event) override {
        audits.push_back(std::make_shared<AuditEvent>(*event));
    }
};

Link makeManagerLink(const AccessMode mode = AccessMode::Public) {
    Link link;
    link.vault_id = 42;
    link.root_entry_id = 77;
    link.root_path = "/reports";
    link.target_type = TargetType::Directory;
    link.link_type = LinkType::Access;
    link.access_mode = mode;
    link.allowed_ops = bit(Operation::Metadata) |
                       bit(Operation::List) |
                       bit(Operation::Download) |
                       bit(Operation::Upload) |
                       bit(Operation::Mkdir);
    link.expires_at = kManagerNow + 3600;
    link.duplicate_policy = DuplicatePolicy::Reject;
    link.metadata = R"({"test":true})";
    return link;
}

class ShareManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<FakeStore> store;
    std::shared_ptr<FakeAuthorizer> authorizer;
    std::unique_ptr<Manager> manager;
    std::shared_ptr<identities::User> user;

    void SetUp() override {
        Token::setPepperForTesting(std::vector<uint8_t>(32, 0x61));
        store = std::make_shared<FakeStore>();
        authorizer = std::make_shared<FakeAuthorizer>();
        ManagerOptions options;
        options.clock = [] { return kManagerNow; };
        options.session_ttl_seconds = 600;
        options.challenge_ttl_seconds = 300;
        options.challenge_max_attempts = 3;
        manager = std::make_unique<Manager>(store, authorizer, options);

        user = std::make_shared<identities::User>();
        user->id = 7;
        user->name = "share-manager-user";
    }

    void TearDown() override {
        Token::clearPepperForTesting();
    }

    [[nodiscard]] rbac::Actor humanActor() const { return rbac::Actor::human(user); }

    [[nodiscard]] CreateLinkResult create(const AccessMode mode = AccessMode::Public) {
        return manager->createLink(humanActor(), {.link = makeManagerLink(mode)});
    }

    [[nodiscard]] std::string wrongSecret(const std::string& token) const {
        auto parsed = Token::parse(token);
        parsed.secret.back() = parsed.secret.back() == 'a' ? 'b' : 'a';
        return (parsed.kind == TokenKind::PublicShare ? "vhs_" : "vhss_") + parsed.lookup_id + "_" + parsed.secret;
    }
};
}

using namespace vh::share::test_manager;

TEST_F(ShareManagerTest, CreatePublicShareSucceedsWhenAuthorizerAllows) {
    const auto result = create();

    ASSERT_NE(result.link, nullptr);
    EXPECT_EQ(result.link->created_by, user->id);
    EXPECT_TRUE(result.public_token.starts_with("vhs_"));
    EXPECT_EQ(result.public_url_path, "/share/" + result.public_token);
    EXPECT_TRUE(Token::verify(result.link->token_hash, TokenKind::PublicShare, Token::parse(result.public_token).secret));
    EXPECT_FALSE(result.link->toManagementJson().dump().contains(Token::parse(result.public_token).secret));
    EXPECT_FALSE(store->audits.empty());
}

TEST_F(ShareManagerTest, CreatePublicShareFailsWhenAuthorizerDenies) {
    authorizer->allow_create = false;
    EXPECT_THROW({ (void)create(); }, std::runtime_error);
    EXPECT_TRUE(store->links.empty());
    ASSERT_FALSE(store->audits.empty());
    EXPECT_EQ(store->audits.back()->status, AuditStatus::Denied);
}

TEST_F(ShareManagerTest, CreateEmailValidatedShareSucceedsWhenAuthorizerAllows) {
    const auto result = create(AccessMode::EmailValidated);

    ASSERT_NE(result.link, nullptr);
    EXPECT_EQ(result.link->access_mode, AccessMode::EmailValidated);
    EXPECT_TRUE(result.public_token.starts_with("vhs_"));
}

TEST_F(ShareManagerTest, InvalidGrantFailsBeforePersistence) {
    auto link = makeManagerLink();
    link.root_entry_id = 0;
    const CreateLinkRequest request{.link = link};

    EXPECT_THROW({ (void)manager->createLink(humanActor(), request); }, std::invalid_argument);
    EXPECT_TRUE(store->links.empty());
}

TEST_F(ShareManagerTest, RevokeLinkMarksInactiveAndRevokesSessions) {
    auto created = create();
    auto opened = manager->openPublicSession(created.public_token);
    ASSERT_TRUE(opened.session->isActive(kManagerNow));

    manager->revokeLink(humanActor(), created.link->id);

    EXPECT_TRUE(created.link->isRevoked());
    EXPECT_FALSE(created.link->isActive(kManagerNow));
    EXPECT_TRUE(opened.session->isRevoked());
}

TEST_F(ShareManagerTest, RotateTokenChangesLookupHashAndInvalidatesOldTokenAndSessions) {
    auto created = create();
    const auto oldToken = created.public_token;
    const auto oldLookup = created.link->token_lookup_id;
    auto opened = manager->openPublicSession(oldToken);

    const auto rotated = manager->rotateLinkToken(humanActor(), created.link->id);

    EXPECT_NE(rotated.link->token_lookup_id, oldLookup);
    EXPECT_TRUE(opened.session->isRevoked());
    EXPECT_THROW({ (void)manager->openPublicSession(oldToken); }, std::runtime_error);
    EXPECT_NO_THROW({ (void)manager->openPublicSession(rotated.public_token); });
}

TEST_F(ShareManagerTest, ValidPublicTokenOpensReadySession) {
    const auto created = create();
    const auto opened = manager->openPublicSession(created.public_token, "127.0.0.1", "test-agent");

    ASSERT_NE(opened.session, nullptr);
    EXPECT_TRUE(opened.ready());
    EXPECT_TRUE(opened.session_token.starts_with("vhss_"));
    EXPECT_EQ(opened.session->share_id, created.link->id);
    EXPECT_EQ(opened.session->ip_address, "127.0.0.1");
    EXPECT_FALSE(opened.link->toPublicJson().dump().contains(Token::parse(opened.session_token).secret));
}

TEST_F(ShareManagerTest, PublicSessionRejectsMalformedWrongOrInactiveTokens) {
    const auto created = create();
    EXPECT_THROW({ (void)manager->openPublicSession("not-a-share-token"); }, std::exception);
    EXPECT_THROW({ (void)manager->openPublicSession(wrongSecret(created.public_token)); }, std::runtime_error);

    created.link->expires_at = kManagerNow - 1;
    EXPECT_THROW({ (void)manager->openPublicSession(created.public_token); }, std::runtime_error);

    created.link->expires_at = kManagerNow + 3600;
    created.link->revoked_at = kManagerNow;
    EXPECT_THROW({ (void)manager->openPublicSession(created.public_token); }, std::runtime_error);

    created.link->revoked_at.reset();
    created.link->disabled_at = kManagerNow;
    EXPECT_THROW({ (void)manager->openPublicSession(created.public_token); }, std::runtime_error);
}

TEST_F(ShareManagerTest, EmailValidatedShareDoesNotResolveBeforeVerification) {
    const auto created = create(AccessMode::EmailValidated);
    const auto opened = manager->openPublicSession(created.public_token);

    EXPECT_FALSE(opened.ready());
    EXPECT_EQ(opened.status, OpenSessionStatus::EmailRequired);
    EXPECT_THROW({ (void)manager->resolvePrincipal(opened.session_token); }, std::runtime_error);
}

TEST_F(ShareManagerTest, EmailChallengeStoresHashesAndRejectsWrongExpiredOrConsumedCodes) {
    const auto created = create(AccessMode::EmailValidated);
    auto challenge = manager->startEmailChallenge(StartEmailChallengeRequest{
        .public_token = created.public_token,
        .session_token = std::nullopt,
        .email = " Recipient@Example.COM ",
        .ip_address = "127.0.0.1",
        .user_agent = "test"
    });

    ASSERT_NE(challenge.challenge, nullptr);
    ASSERT_NE(challenge.session, nullptr);
    EXPECT_FALSE(challenge.verification_code.empty());
    EXPECT_FALSE(challenge.challenge->email_hash.empty());
    EXPECT_FALSE(challenge.challenge->code_hash.empty());
    EXPECT_NE(challenge.challenge->code_hash, std::vector<uint8_t>(challenge.verification_code.begin(), challenge.verification_code.end()));
    EXPECT_EQ(challenge.challenge->attempts, 0u);

    EXPECT_THROW({ (void)manager->confirmEmailChallenge(ConfirmEmailChallengeRequest{
        .challenge_id = challenge.challenge->id,
        .session_id = challenge.session->id,
        .code = "000000"
    }); }, std::runtime_error);
    EXPECT_EQ(challenge.challenge->attempts, 1u);

    challenge.challenge->expires_at = kManagerNow - 1;
    EXPECT_THROW({ (void)manager->confirmEmailChallenge(ConfirmEmailChallengeRequest{
        .challenge_id = challenge.challenge->id,
        .session_id = challenge.session->id,
        .code = challenge.verification_code
    }); }, std::runtime_error);

    auto consumed = manager->startEmailChallenge(StartEmailChallengeRequest{
        .public_token = std::nullopt,
        .session_token = challenge.session_token,
        .email = "recipient@example.com",
        .ip_address = std::nullopt,
        .user_agent = std::nullopt
    });
    store->consumeEmailChallenge(consumed.challenge->id);
    EXPECT_THROW({ (void)manager->confirmEmailChallenge(ConfirmEmailChallengeRequest{
        .challenge_id = consumed.challenge->id,
        .session_id = consumed.session->id,
        .code = consumed.verification_code
    }); }, std::runtime_error);
}

TEST_F(ShareManagerTest, ConfirmEmailChallengeVerifiesSessionAndPreventsReuse) {
    const auto created = create(AccessMode::EmailValidated);
    auto challenge = manager->startEmailChallenge(StartEmailChallengeRequest{
        .public_token = created.public_token,
        .session_token = std::nullopt,
        .email = "recipient@example.com",
        .ip_address = std::nullopt,
        .user_agent = std::nullopt
    });

    auto confirmed = manager->confirmEmailChallenge({
        .challenge_id = challenge.challenge->id,
        .session_id = challenge.session->id,
        .code = challenge.verification_code
    });

    ASSERT_NE(confirmed.session, nullptr);
    EXPECT_TRUE(confirmed.verified);
    EXPECT_TRUE(confirmed.session->isVerified());
    EXPECT_TRUE(challenge.challenge->consumed_at.has_value());
    EXPECT_THROW({ (void)manager->confirmEmailChallenge(ConfirmEmailChallengeRequest{
        .challenge_id = challenge.challenge->id,
        .session_id = challenge.session->id,
        .code = challenge.verification_code
    }); }, std::runtime_error);
}

TEST_F(ShareManagerTest, SessionTokenResolvesPrincipalAndRejectsInactiveSessions) {
    const auto created = create();
    auto opened = manager->openPublicSession(created.public_token);

    auto principal = manager->resolvePrincipal(opened.session_token);
    ASSERT_NE(principal, nullptr);
    EXPECT_EQ(principal->share_id, created.link->id);
    EXPECT_EQ(principal->share_session_id, opened.session->id);

    opened.session->expires_at = kManagerNow - 1;
    EXPECT_THROW({ (void)manager->resolvePrincipal(opened.session_token); }, std::runtime_error);

    opened.session->expires_at = kManagerNow + 600;
    opened.session->revoked_at = kManagerNow;
    EXPECT_THROW({ (void)manager->resolvePrincipal(opened.session_token); }, std::runtime_error);
}

TEST_F(ShareManagerTest, AuthorizeUsesScopeForOperationsAndContainment) {
    const auto created = create();
    auto opened = manager->openPublicSession(created.public_token);
    auto principal = manager->resolvePrincipal(opened.session_token);

    auto decision = manager->authorize(*principal, Operation::Download, "//reports//2026///summary.pdf", TargetType::File);
    EXPECT_TRUE(decision.allowed);
    EXPECT_EQ(decision.normalized_path, "/reports/2026/summary.pdf");

    decision = manager->authorize(*principal, Operation::Preview, "/reports/2026/summary.pdf", TargetType::File);
    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, "operation_not_granted");

    decision = manager->authorize(*principal, Operation::Download, "/reports/summary.pdf", TargetType::File, 999);
    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, "vault_mismatch");

    decision = manager->authorize(*principal, Operation::Download, "../reports/summary.pdf", TargetType::File);
    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, "path_escape");

    principal->root_path = "/shared";
    principal->grant.root_path = "/shared";
    decision = manager->authorize(*principal, Operation::Download, "/shared_evil/summary.pdf", TargetType::File);
    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, "outside_scope");
}

TEST_F(ShareManagerTest, ShareActorCannotUseManagementAuthorizationOrHumanPrivileges) {
    const auto created = create();
    auto opened = manager->openPublicSession(created.public_token);
    auto principal = manager->resolvePrincipal(opened.session_token);
    const auto shareActor = rbac::Actor::share(principal);

    EXPECT_FALSE(shareActor.canUseHumanPrivileges());
    EXPECT_EQ(shareActor.user(), nullptr);
    const CreateLinkRequest request{.link = makeManagerLink()};
    EXPECT_THROW({ (void)manager->createLink(shareActor, request); }, std::runtime_error);
    EXPECT_THROW({ (void)manager->listLinksForUser(shareActor, {}); }, std::runtime_error);
}
