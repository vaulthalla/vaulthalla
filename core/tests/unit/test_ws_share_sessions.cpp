#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Manager.hpp"
#include "identities/User.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/handler/share/Sessions.hpp"
#include "runtime/Deps.hpp"
#include "share/AuditEvent.hpp"
#include "share/EmailChallenge.hpp"
#include "share/Manager.hpp"
#include "share/Principal.hpp"
#include "share/Token.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace vh::protocols::ws::handler::share::test_sessions {
constexpr std::time_t kNow = 1'800'000'000;
constexpr std::string_view kChallengeCode = "123456";

std::string uuidFor(const uint32_t value) {
    return std::format("00000000-0000-4000-8000-{:012d}", value);
}

class FakeAuthorizer final : public vh::share::ShareAuthorizer {
public:
    vh::share::AuthorizationDecision canCreateLink(
        const rbac::Actor& actor,
        const vh::share::Link&
    ) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }

    vh::share::AuthorizationDecision canUpdateLink(
        const rbac::Actor& actor,
        const vh::share::Link&,
        const vh::share::Link&
    ) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }

    vh::share::AuthorizationDecision canManageLink(
        const rbac::Actor& actor,
        const vh::share::Link&
    ) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }

    vh::share::AuthorizationDecision canListVaultLinks(const rbac::Actor& actor, uint32_t) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }
};

class FakeStore final : public vh::share::ShareStore {
public:
    std::unordered_map<std::string, std::shared_ptr<vh::share::Link>> links;
    std::unordered_map<std::string, std::string> link_by_lookup;
    std::unordered_map<std::string, std::shared_ptr<vh::share::Session>> sessions;
    std::unordered_map<std::string, std::string> session_by_lookup;
    std::unordered_map<std::string, std::shared_ptr<vh::share::EmailChallenge>> challenges;
    std::vector<std::shared_ptr<vh::share::AuditEvent>> audits;
    uint32_t next_link{100};
    uint32_t next_session{200};
    uint32_t next_challenge{300};

    std::shared_ptr<vh::share::Link> createLink(const std::shared_ptr<vh::share::Link>& link) override {
        auto stored = std::make_shared<vh::share::Link>(*link);
        if (stored->id.empty()) stored->id = uuidFor(next_link++);
        stored->created_at = kNow;
        stored->updated_at = kNow;
        links[stored->id] = stored;
        link_by_lookup[stored->token_lookup_id] = stored->id;
        return stored;
    }

    std::shared_ptr<vh::share::Link> getLink(const std::string& id) override {
        if (!links.contains(id)) return nullptr;
        return links.at(id);
    }

    std::shared_ptr<vh::share::Link> getLinkByLookupId(const std::string& lookupId) override {
        if (!link_by_lookup.contains(lookupId)) return nullptr;
        return getLink(link_by_lookup.at(lookupId));
    }

    std::vector<std::shared_ptr<vh::share::Link>> listLinksForUser(
        uint32_t userId,
        const db::model::ListQueryParams&
    ) override {
        std::vector<std::shared_ptr<vh::share::Link>> out;
        for (auto& [_, link] : links)
            if (link->created_by == userId) out.push_back(link);
        return out;
    }

    std::vector<std::shared_ptr<vh::share::Link>> listLinksForVault(
        uint32_t vaultId,
        const db::model::ListQueryParams&
    ) override {
        std::vector<std::shared_ptr<vh::share::Link>> out;
        for (auto& [_, link] : links)
            if (link->vault_id == vaultId) out.push_back(link);
        return out;
    }

    std::shared_ptr<vh::share::Link> updateLink(const std::shared_ptr<vh::share::Link>& link) override {
        links[link->id] = link;
        link_by_lookup[link->token_lookup_id] = link->id;
        return link;
    }

    void revokeLink(const std::string& id, uint32_t revokedBy) override {
        auto link = getLink(id);
        if (!link) throw std::runtime_error("missing link");
        link->revoked_at = kNow;
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
        ++link->access_count;
        link->last_accessed_at = kNow;
    }

    void incrementDownload(const std::string& id) override {
        auto link = getLink(id);
        if (!link) throw std::runtime_error("missing link");
        ++link->download_count;
    }

    std::shared_ptr<vh::share::Session> createSession(
        const std::shared_ptr<vh::share::Session>& session
    ) override {
        auto stored = std::make_shared<vh::share::Session>(*session);
        if (stored->id.empty()) stored->id = uuidFor(next_session++);
        stored->created_at = kNow;
        stored->last_seen_at = kNow;
        sessions[stored->id] = stored;
        session_by_lookup[stored->session_token_lookup_id] = stored->id;
        return stored;
    }

    std::shared_ptr<vh::share::Session> getSession(const std::string& id) override {
        if (!sessions.contains(id)) return nullptr;
        return sessions.at(id);
    }

    std::shared_ptr<vh::share::Session> getSessionByLookupId(const std::string& lookupId) override {
        if (!session_by_lookup.contains(lookupId)) return nullptr;
        return getSession(session_by_lookup.at(lookupId));
    }

    void verifySession(const std::string& sessionId, const std::vector<uint8_t>& emailHash) override {
        auto session = getSession(sessionId);
        if (!session) throw std::runtime_error("missing session");
        session->email_hash = emailHash;
        session->verified_at = kNow;
    }

    void touchSession(const std::string& sessionId) override {
        auto session = getSession(sessionId);
        if (!session) throw std::runtime_error("missing session");
        session->last_seen_at = kNow + 1;
    }

    void revokeSession(const std::string& sessionId) override {
        auto session = getSession(sessionId);
        if (!session) throw std::runtime_error("missing session");
        session->revoked_at = kNow;
    }

    void revokeSessionsForShare(const std::string& shareId) override {
        for (auto& [_, session] : sessions)
            if (session->share_id == shareId && !session->revoked_at) session->revoked_at = kNow;
    }

    std::shared_ptr<vh::share::EmailChallenge> createEmailChallenge(
        const std::shared_ptr<vh::share::EmailChallenge>& challenge
    ) override {
        auto stored = std::make_shared<vh::share::EmailChallenge>(*challenge);
        if (stored->id.empty()) stored->id = uuidFor(next_challenge++);
        stored->created_at = kNow;
        challenges[stored->id] = stored;
        return stored;
    }

    std::shared_ptr<vh::share::EmailChallenge> getEmailChallenge(const std::string& id) override {
        if (!challenges.contains(id)) return nullptr;
        return challenges.at(id);
    }

    std::shared_ptr<vh::share::EmailChallenge> getActiveEmailChallenge(
        const std::string& shareId,
        const std::vector<uint8_t>& emailHash
    ) override {
        std::shared_ptr<vh::share::EmailChallenge> best;
        for (auto& [_, challenge] : challenges) {
            if (challenge->share_id != shareId) continue;
            if (challenge->email_hash != emailHash) continue;
            if (!challenge->canAttempt(kNow)) continue;
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
        challenge->consumed_at = kNow;
    }

    void appendAuditEvent(const std::shared_ptr<vh::share::AuditEvent>& event) override {
        audits.push_back(std::make_shared<vh::share::AuditEvent>(*event));
    }
};

vh::share::Link makeLink(const vh::share::AccessMode mode = vh::share::AccessMode::Public) {
    vh::share::Link link;
    link.vault_id = 42;
    link.root_entry_id = 77;
    link.root_path = "/reports";
    link.target_type = vh::share::TargetType::Directory;
    link.link_type = vh::share::LinkType::Access;
    link.access_mode = mode;
    link.allowed_ops = vh::share::bit(vh::share::Operation::Metadata) |
                       vh::share::bit(vh::share::Operation::List) |
                       vh::share::bit(vh::share::Operation::Download);
    link.expires_at = kNow + 3600;
    return link;
}

std::shared_ptr<Session> publicSession() {
    auto session = std::make_shared<Session>(std::make_shared<Router>());
    session->ipAddress = "127.0.0.1";
    session->userAgent = "ws-share-test";
    return session;
}

void attachRefreshToken(const std::shared_ptr<Session>& session, const std::string& jti) {
    auto token = std::make_shared<vh::auth::model::RefreshToken>();
    token->jti = jti;
    token->rawToken = "raw-" + jti;
    token->hashedToken = "hashed-" + jti;
    token->subject = session->ipAddress + ":" + session->userAgent;
    token->issuedAt = std::chrono::system_clock::now() - std::chrono::minutes(1);
    token->expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
    token->userAgent = session->userAgent;
    token->ipAddress = session->ipAddress;
    session->tokens->refreshToken = std::move(token);
}

std::shared_ptr<Session> humanSession() {
    auto session = publicSession();
    auto user = std::make_shared<identities::User>();
    user->id = 9;
    user->name = "human";
    session->user = user;
    return session;
}

std::shared_ptr<vh::share::Principal> principalForRouterTest() {
    auto principal = std::make_shared<vh::share::Principal>();
    principal->share_id = uuidFor(1);
    principal->share_session_id = uuidFor(2);
    principal->vault_id = 42;
    principal->root_entry_id = 77;
    principal->root_path = "/reports";
    principal->expires_at = kNow + 3600;
    return principal;
}

void expectNoSecretFields(const nlohmann::json& value) {
    EXPECT_FALSE(value.dump().contains("token_lookup_id"));
    EXPECT_FALSE(value.dump().contains("token_hash"));
    EXPECT_FALSE(value.dump().contains("verification_code"));
    EXPECT_FALSE(value.dump().contains("email@example.com"));
}

class WsShareSessionsTest : public ::testing::Test {
protected:
    std::shared_ptr<FakeStore> store;
    std::shared_ptr<FakeAuthorizer> authorizer;
    std::shared_ptr<vh::share::Manager> manager;
    std::shared_ptr<identities::User> user;
    std::shared_ptr<vh::auth::session::Manager> oldSessionManager;

    void SetUp() override {
        vh::share::Token::setPepperForTesting(std::vector<uint8_t>(32, 0x61));
        oldSessionManager = vh::runtime::Deps::get().sessionManager;
        vh::runtime::Deps::get().sessionManager = std::make_shared<vh::auth::session::Manager>();
        store = std::make_shared<FakeStore>();
        authorizer = std::make_shared<FakeAuthorizer>();
        vh::share::ManagerOptions options;
        options.clock = [] { return kNow; };
        options.session_ttl_seconds = 600;
        options.challenge_ttl_seconds = 300;
        options.challenge_max_attempts = 3;
        options.challenge_code_generator = [] { return std::string(kChallengeCode); };
        manager = std::make_shared<vh::share::Manager>(store, authorizer, options);
        Sessions::setManagerFactoryForTesting([this] { return manager; });

        user = std::make_shared<identities::User>();
        user->id = 7;
        user->name = "share-owner";
    }

    void TearDown() override {
        Sessions::resetManagerFactoryForTesting();
        vh::runtime::Deps::get().sessionManager = oldSessionManager;
        vh::share::Token::clearPepperForTesting();
    }

    vh::share::CreateLinkResult create(const vh::share::AccessMode mode = vh::share::AccessMode::Public) {
        return manager->createLink(rbac::Actor::human(user), {.link = makeLink(mode)});
    }

    std::string wrongSecret(const std::string& token) const {
        auto parsed = vh::share::Token::parse(token);
        parsed.secret.back() = parsed.secret.back() == 'a' ? 'b' : 'a';
        return (parsed.kind == vh::share::TokenKind::PublicShare ? "vhs_" : "vhss_") +
               parsed.lookup_id + "_" + parsed.secret;
    }
};

TEST_F(WsShareSessionsTest, RouterClassifiesPublicHumanAndShareCommands) {
    auto unauth = publicSession();
    auto human = humanSession();
    auto pending = publicSession();
    pending->setPendingShareSession(uuidFor(10), "vhss_pending");
    auto share = publicSession();
    share->setSharePrincipal(principalForRouterTest(), "vhss_ready");

    using Decision = Router::CommandAuthDecision;
    EXPECT_EQ(Decision::Allow, Router::classifyCommand("share.session.open", *unauth));
    EXPECT_EQ(Decision::Deny, Router::classifyCommand("fs.dir.list", *unauth));
    EXPECT_EQ(Decision::Deny, Router::classifyCommand("share.link.create", *unauth));

    EXPECT_EQ(Decision::Deny, Router::classifyCommand("share.session.open", *human));
    EXPECT_EQ(Decision::RequireHumanAuth, Router::classifyCommand("share.link.create", *human));
    EXPECT_EQ(Decision::RequireHumanAuth, Router::classifyCommand("fs.dir.list", *human));

    EXPECT_EQ(Decision::Allow, Router::classifyCommand("share.email.challenge.confirm", *pending));
    EXPECT_EQ(Decision::Deny, Router::classifyCommand("share.link.create", *pending));
    EXPECT_EQ(Decision::Deny, Router::classifyCommand("auth.logout", *share));
    EXPECT_EQ(Decision::Deny, Router::classifyCommand("fs.dir.list", *share));
}

TEST_F(WsShareSessionsTest, ValidPublicTokenOpensReadyShareSession) {
    const auto created = create();
    auto session = publicSession();

    const auto response = Sessions::open({{"public_token", created.public_token}}, session);

    EXPECT_EQ("ready", response.at("status").get<std::string>());
    EXPECT_TRUE(response.at("session_token").get<std::string>().starts_with("vhss_"));
    EXPECT_TRUE(session->isShareMode());
    ASSERT_NE(session->sharePrincipal(), nullptr);
    EXPECT_EQ(created.link->id, session->sharePrincipal()->share_id);
    EXPECT_EQ(nullptr, session->user);
    expectNoSecretFields(response);
}

TEST_F(WsShareSessionsTest, PublicOpenMovesExistingRefreshJtiToReadyShareStorage) {
    const auto created = create();
    auto session = publicSession();
    attachRefreshToken(session, "refresh-jti-ready");
    vh::runtime::Deps::get().sessionManager->cache(session);

    ASSERT_EQ(session, vh::runtime::Deps::get().sessionManager->get("refresh-jti-ready"));
    ASSERT_EQ(nullptr, vh::runtime::Deps::get().sessionManager->getShareByRefreshJti("refresh-jti-ready"));

    const auto response = Sessions::open({{"public_token", created.public_token}}, session);

    EXPECT_EQ("ready", response.at("status").get<std::string>());
    EXPECT_EQ(nullptr, vh::runtime::Deps::get().sessionManager->get("refresh-jti-ready"));
    EXPECT_EQ(session, vh::runtime::Deps::get().sessionManager->getShareByRefreshJti("refresh-jti-ready"));
    EXPECT_TRUE(session->isShareMode());
    EXPECT_FALSE(session->user);
}

TEST_F(WsShareSessionsTest, PublicSessionRejectsMalformedWrongOrInactiveTokens) {
    const auto created = create();

    EXPECT_THROW(Sessions::open({{"public_token", "not-a-share-token"}}, publicSession()), std::exception);
    EXPECT_THROW(Sessions::open({{"public_token", wrongSecret(created.public_token)}}, publicSession()), std::runtime_error);

    created.link->expires_at = kNow - 1;
    EXPECT_THROW(Sessions::open({{"public_token", created.public_token}}, publicSession()), std::runtime_error);

    created.link->expires_at = kNow + 3600;
    created.link->revoked_at = kNow;
    EXPECT_THROW(Sessions::open({{"public_token", created.public_token}}, publicSession()), std::runtime_error);

    created.link->revoked_at.reset();
    created.link->disabled_at = kNow;
    EXPECT_THROW(Sessions::open({{"public_token", created.public_token}}, publicSession()), std::runtime_error);
}

TEST_F(WsShareSessionsTest, EmailValidatedShareOpensPendingAndDoesNotGrantPrincipal) {
    const auto created = create(vh::share::AccessMode::EmailValidated);
    auto session = publicSession();

    const auto response = Sessions::open({{"public_token", created.public_token}}, session);

    EXPECT_EQ("email_required", response.at("status").get<std::string>());
    EXPECT_TRUE(response.at("session_token").get<std::string>().starts_with("vhss_"));
    EXPECT_TRUE(session->isSharePending());
    EXPECT_EQ(nullptr, session->sharePrincipal());
    EXPECT_THROW({ (void)manager->resolvePrincipal(session->shareSessionToken()); }, std::runtime_error);
    expectNoSecretFields(response);
}

TEST_F(WsShareSessionsTest, EmailConfirmPromotesExistingRefreshJtiToReadyShareStorage) {
    const auto created = create(vh::share::AccessMode::EmailValidated);
    auto session = publicSession();
    attachRefreshToken(session, "refresh-jti-email");
    vh::runtime::Deps::get().sessionManager->cache(session);

    const auto opened = Sessions::open({{"public_token", created.public_token}}, session);

    EXPECT_EQ("email_required", opened.at("status").get<std::string>());
    ASSERT_EQ(nullptr, vh::runtime::Deps::get().sessionManager->get("refresh-jti-email"));
    ASSERT_EQ(session, vh::runtime::Deps::get().sessionManager->getShareByRefreshJti("refresh-jti-email"));
    EXPECT_TRUE(session->isSharePending());

    const auto challenge = Sessions::startEmailChallenge({{"email", " Email@Example.COM "}}, session);
    const auto confirmed = Sessions::confirmEmailChallenge({
        {"challenge_id", challenge.at("challenge_id").get<std::string>()},
        {"code", std::string(kChallengeCode)}
    }, session);

    EXPECT_EQ("ready", confirmed.at("status").get<std::string>());
    EXPECT_EQ(nullptr, vh::runtime::Deps::get().sessionManager->get("refresh-jti-email"));
    EXPECT_EQ(session, vh::runtime::Deps::get().sessionManager->getShareByRefreshJti("refresh-jti-email"));
    EXPECT_TRUE(session->isShareMode());
    EXPECT_FALSE(session->user);
}

TEST_F(WsShareSessionsTest, ChallengeStartAndConfirmEstablishShareModeWithoutReturningCode) {
    const auto created = create(vh::share::AccessMode::EmailValidated);
    auto session = publicSession();
    Sessions::open({{"public_token", created.public_token}}, session);

    const auto challenge = Sessions::startEmailChallenge({{"email", " Email@Example.COM "}}, session);

    EXPECT_EQ("challenge_created", challenge.at("status").get<std::string>());
    EXPECT_TRUE(session->isSharePending());
    EXPECT_FALSE(challenge.contains("verification_code"));
    expectNoSecretFields(challenge);

    EXPECT_THROW(Sessions::confirmEmailChallenge({
        {"challenge_id", challenge.at("challenge_id").get<std::string>()},
        {"code", "000000"}
    }, session), std::runtime_error);

    const auto confirmed = Sessions::confirmEmailChallenge({
        {"challenge_id", challenge.at("challenge_id").get<std::string>()},
        {"code", std::string(kChallengeCode)}
    }, session);

    EXPECT_EQ("ready", confirmed.at("status").get<std::string>());
    EXPECT_TRUE(confirmed.at("verified").get<bool>());
    EXPECT_TRUE(session->isShareMode());
    ASSERT_NE(session->sharePrincipal(), nullptr);
    EXPECT_TRUE(session->sharePrincipal()->email_verified);
    expectNoSecretFields(confirmed);
}

TEST_F(WsShareSessionsTest, ChallengeFlowRejectsInvalidStates) {
    const auto publicShare = create();
    auto ready = publicSession();
    Sessions::open({{"public_token", publicShare.public_token}}, ready);

    EXPECT_THROW(Sessions::startEmailChallenge({{"email", "email@example.com"}}, ready), std::runtime_error);
    EXPECT_THROW(Sessions::open({{"public_token", publicShare.public_token}}, humanSession()), std::runtime_error);
    EXPECT_THROW(Sessions::confirmEmailChallenge({
        {"challenge_id", uuidFor(999)},
        {"code", std::string(kChallengeCode)}
    }, publicSession()), std::invalid_argument);
}

}
