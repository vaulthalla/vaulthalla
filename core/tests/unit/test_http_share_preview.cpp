#include "fs/model/Directory.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Path.hpp"
#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "auth/session/Issuer.hpp"
#include "auth/session/Manager.hpp"
#include "config/Registry.hpp"
#include "identities/User.hpp"
#include "protocols/http/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "share/AuditEvent.hpp"
#include "share/EmailChallenge.hpp"
#include "share/Manager.hpp"
#include "share/TargetResolver.hpp"
#include "share/Token.hpp"
#include "stats/model/CacheStats.hpp"
#include "storage/Engine.hpp"
#include "vault/model/Vault.hpp"
#include "protocols/ws/Router.hpp"

#include <gtest/gtest.h>
#include <paths.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace vh::protocols::http::test_share_preview {
constexpr std::time_t kNow = 1'800'000'000;
constexpr uint32_t kVaultId = 42;
constexpr uint32_t kRootEntryId = 10;

class FakeAuthorizer final : public vh::share::ShareAuthorizer {
public:
    vh::share::AuthorizationDecision canCreateLink(const rbac::Actor& actor, const vh::share::Link&) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }

    vh::share::AuthorizationDecision canUpdateLink(
        const rbac::Actor& actor,
        const vh::share::Link&,
        const vh::share::Link&
    ) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }

    vh::share::AuthorizationDecision canManageLink(const rbac::Actor& actor, const vh::share::Link&) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }

    vh::share::AuthorizationDecision canListVaultLinks(const rbac::Actor& actor, uint32_t) const override {
        return {actor.isHuman(), actor.isHuman() ? "allowed" : "not_human"};
    }
};

class FakeStore final : public vh::share::ShareStore {
public:
    std::unordered_map<std::string, std::shared_ptr<vh::share::Link>> links;
    std::unordered_map<std::string, std::shared_ptr<vh::share::Session>> sessions;
    std::unordered_map<std::string, std::string> sessionByLookup;
    std::unordered_map<std::string, std::shared_ptr<vh::rbac::role::Vault>> vaultRoles;
    std::vector<std::shared_ptr<vh::share::AuditEvent>> audits;

    std::shared_ptr<vh::share::Link> createLink(const std::shared_ptr<vh::share::Link>& link) override {
        links[link->id] = std::make_shared<vh::share::Link>(*link);
        return links.at(link->id);
    }

    std::shared_ptr<vh::share::Link> getLink(const std::string& id) override {
        if (!links.contains(id)) return nullptr;
        return links.at(id);
    }

    std::shared_ptr<vh::share::Link> getLinkByLookupId(const std::string&) override { return nullptr; }

    std::vector<std::shared_ptr<vh::share::Link>> listLinksForUser(
        uint32_t,
        const db::model::ListQueryParams&
    ) override {
        return {};
    }

    std::vector<std::shared_ptr<vh::share::Link>> listLinksForVault(
        uint32_t,
        const db::model::ListQueryParams&
    ) override {
        return {};
    }

    std::shared_ptr<vh::share::Link> updateLink(const std::shared_ptr<vh::share::Link>& link) override {
        links[link->id] = std::make_shared<vh::share::Link>(*link);
        return links.at(link->id);
    }

    void revokeLink(const std::string&, uint32_t) override {}
    void rotateLinkToken(const std::string&, const std::string&, const std::vector<uint8_t>&, uint32_t) override {}
    void touchLinkAccess(const std::string&) override {}
    void incrementDownload(const std::string&) override {}

    void upsertVaultRoleForShare(
        const std::string& shareId,
        uint32_t,
        const std::shared_ptr<vh::rbac::role::Vault>& role
    ) override {
        vaultRoles[shareId] = std::make_shared<vh::rbac::role::Vault>(*role);
    }

    std::shared_ptr<vh::rbac::role::Vault> getVaultRoleForShare(const std::string& shareId) override {
        if (!vaultRoles.contains(shareId)) return nullptr;
        return std::make_shared<vh::rbac::role::Vault>(*vaultRoles.at(shareId));
    }

    std::shared_ptr<vh::share::Session> createSession(const std::shared_ptr<vh::share::Session>& session) override {
        sessions[session->id] = std::make_shared<vh::share::Session>(*session);
        sessionByLookup[session->session_token_lookup_id] = session->id;
        return sessions.at(session->id);
    }

    std::shared_ptr<vh::share::Session> getSession(const std::string& id) override {
        if (!sessions.contains(id)) return nullptr;
        return sessions.at(id);
    }

    std::shared_ptr<vh::share::Session> getSessionByLookupId(const std::string& lookupId) override {
        if (!sessionByLookup.contains(lookupId)) return nullptr;
        return getSession(sessionByLookup.at(lookupId));
    }

    void verifySession(const std::string&, const std::vector<uint8_t>&) override {}
    void touchSession(const std::string& sessionId) override {
        if (auto session = getSession(sessionId)) session->last_seen_at = kNow + 1;
    }
    void revokeSession(const std::string&) override {}
    void revokeSessionsForShare(const std::string&) override {}

    std::shared_ptr<vh::share::EmailChallenge> createEmailChallenge(
        const std::shared_ptr<vh::share::EmailChallenge>&
    ) override {
        return nullptr;
    }
    std::shared_ptr<vh::share::EmailChallenge> getEmailChallenge(const std::string&) override { return nullptr; }
    std::shared_ptr<vh::share::EmailChallenge> getActiveEmailChallenge(
        const std::string&,
        const std::vector<uint8_t>&
    ) override {
        return nullptr;
    }
    void recordEmailChallengeAttempt(const std::string&) override {}
    void consumeEmailChallenge(const std::string&) override {}
    void appendAuditEvent(const std::shared_ptr<vh::share::AuditEvent>& event) override { audits.push_back(event); }
};

class FakeTargetProvider final : public vh::share::TargetEntryProvider {
public:
    std::unordered_map<uint32_t, std::shared_ptr<vh::fs::model::Entry>> byId;
    std::unordered_map<std::string, std::shared_ptr<vh::fs::model::Entry>> byPath;

    std::shared_ptr<vh::fs::model::Entry> getEntryById(const uint32_t entryId) override {
        if (!byId.contains(entryId)) return nullptr;
        return byId.at(entryId);
    }

    std::shared_ptr<vh::fs::model::Entry> getEntryByVaultPath(
        uint32_t,
        const std::string_view vaultPath
    ) override {
        const auto key = std::string(vaultPath);
        if (!byPath.contains(key)) return nullptr;
        return byPath.at(key);
    }

    std::vector<std::shared_ptr<vh::fs::model::Entry>> listChildren(uint32_t) override { return {}; }
};

class HttpSharePreviewTest : public ::testing::Test {
protected:
    std::shared_ptr<FakeStore> store;
    std::shared_ptr<FakeTargetProvider> provider;
    std::shared_ptr<vh::share::Manager> manager;
    std::shared_ptr<vh::storage::Engine> engine;
    std::shared_ptr<vh::fs::model::File> file;
    std::shared_ptr<vh::auth::session::Manager> oldSessionManager;
    vh::share::GeneratedToken sessionToken;
    std::filesystem::path oldBackingPath;
    std::filesystem::path oldMountPath;
    std::filesystem::path testRoot;
    unsigned previewSize{128};

    void SetUp() override {
        vh::share::Token::setPepperForTesting(std::vector<uint8_t>(32, 7));
        vh::runtime::Deps::get().httpCacheStats = std::make_shared<vh::stats::model::CacheStats>();
        oldSessionManager = vh::runtime::Deps::get().sessionManager;
        vh::runtime::Deps::get().sessionManager = std::make_shared<vh::auth::session::Manager>();
        vh::auth::session::Issuer::setJwtSecretForTesting("http-share-preview-test-secret");
        oldBackingPath = vh::paths::backingPath;
        oldMountPath = vh::paths::mountPath;
        testRoot = std::filesystem::temp_directory_path() / "vh_http_share_preview_unit";
        std::filesystem::remove_all(testRoot);
        vh::paths::backingPath = testRoot / "backing";
        vh::paths::mountPath = testRoot / "mount";
        std::filesystem::create_directories(vh::paths::backingPath);
        std::filesystem::create_directories(vh::paths::mountPath);
        const auto& configuredSizes = vh::config::Registry::get().caching.thumbnails.sizes;
        if (!configuredSizes.empty()) previewSize = configuredSizes.front();

        store = std::make_shared<FakeStore>();
        provider = std::make_shared<FakeTargetProvider>();
        auto options = vh::share::ManagerOptions{.clock = [] { return kNow; }};
        manager = std::make_shared<vh::share::Manager>(store, std::make_shared<FakeAuthorizer>(), options);

        auto vault = std::make_shared<vh::vault::model::Vault>();
        vault->id = kVaultId;
        vault->mount_point = "http-share-preview-test";
        engine = std::make_shared<vh::storage::Engine>();
        engine->vault = vault;
        engine->paths = std::make_shared<vh::fs::model::Path>("http-share-preview-test", "http-share-preview-test");

        auto root = std::make_shared<vh::fs::model::Directory>();
        root->id = kRootEntryId;
        root->name = "shared";
        root->vault_id = static_cast<int32_t>(kVaultId);
        root->path = "/shared";

        file = std::make_shared<vh::fs::model::File>();
        file->id = 11;
        file->parent_id = static_cast<int32_t>(kRootEntryId);
        file->name = "report.jpg";
        file->base32_alias = "report-alias";
        file->vault_id = static_cast<int32_t>(kVaultId);
        file->path = "/shared/report.jpg";
        file->mime_type = "image/jpeg";
        file->size_bytes = 128;

        provider->byId[root->id] = root;
        provider->byPath[root->path.string()] = root;
        provider->byId[file->id] = file;
        provider->byPath[file->path.string()] = file;

        const auto thumbDir = engine->paths->thumbnailRoot / file->base32_alias;
        std::filesystem::create_directories(thumbDir);
        std::ofstream(thumbDir / (std::to_string(previewSize) + ".jpg"), std::ios::binary) << "cached-thumbnail";
    }

    void TearDown() override {
        Router::resetPreviewSessionResolverForTesting();
        Router::resetSharePreviewManagerFactoryForTesting();
        Router::resetSharePreviewResolverFactoryForTesting();
        Router::resetPreviewEngineResolverForTesting();
        vh::share::Token::clearPepperForTesting();
        if (engine && engine->paths)
            std::filesystem::remove_all(engine->paths->thumbnailRoot / file->base32_alias);
        vh::runtime::Deps::get().sessionManager = oldSessionManager;
        vh::auth::session::Issuer::clearJwtSecretForTesting();
        vh::paths::backingPath = oldBackingPath;
        vh::paths::mountPath = oldMountPath;
        std::filesystem::remove_all(testRoot);
    }

    void installSharePreviewHooks(const std::shared_ptr<vh::protocols::ws::Session>& session) {
        Router::setPreviewSessionResolverForTesting([session](const request&) { return session; });
        installSharePreviewDependencies();
    }

    void installSharePreviewDependencies() {
        Router::setSharePreviewManagerFactoryForTesting([manager = manager] { return manager; });
        Router::setSharePreviewResolverFactoryForTesting([provider = provider] {
            return std::make_shared<vh::share::TargetResolver>(provider);
        });
        Router::setPreviewEngineResolverForTesting([engine = engine](const uint32_t vaultId) {
            return vaultId == kVaultId ? engine : nullptr;
        });
    }

    std::shared_ptr<vh::protocols::ws::Session> readySession(const uint32_t allowedOps) {
        auto link = std::make_shared<vh::share::Link>();
        link->id = "share-1";
        link->vault_id = kVaultId;
        link->root_entry_id = kRootEntryId;
        link->root_path = "/shared";
        link->target_type = vh::share::TargetType::Directory;
        link->link_type = vh::share::LinkType::Access;
        link->access_mode = vh::share::AccessMode::Public;
        link->allowed_ops = allowedOps;
        link->created_at = kNow;
        link->updated_at = kNow;
        store->createLink(link);
        auto role = std::make_shared<vh::rbac::role::Vault>(vh::rbac::role::Vault::Reader());
        role->id = 2;
        role->assign(2, "public", link->vault_id);
        store->upsertVaultRoleForShare(link->id, link->vault_id, role);

        sessionToken = vh::share::Token::generate(vh::share::TokenKind::ShareSession);
        auto shareSession = std::make_shared<vh::share::Session>();
        shareSession->id = "share-session-1";
        shareSession->share_id = link->id;
        shareSession->session_token_lookup_id = sessionToken.lookup_id;
        shareSession->session_token_hash = sessionToken.hash;
        shareSession->created_at = kNow;
        shareSession->last_seen_at = kNow;
        shareSession->expires_at = kNow + 3600;
        store->createSession(shareSession);

        auto principal = std::make_shared<vh::share::Principal>();
        principal->share_id = link->id;
        principal->share_session_id = shareSession->id;
        principal->vault_id = kVaultId;
        principal->root_entry_id = kRootEntryId;
        principal->root_path = "/shared";
        principal->grant = link->grant();
        principal->expires_at = kNow + 3600;
        principal->scoped_vault_role = role;

        auto session = std::make_shared<vh::protocols::ws::Session>(std::make_shared<vh::protocols::ws::Router>());
        session->ipAddress = "127.0.0.1";
        session->userAgent = "http-share-preview-test";
        session->setSharePrincipal(principal, sessionToken.raw);
        return session;
    }

    std::string issueShareRefresh(const std::shared_ptr<vh::protocols::ws::Session>& session) {
        vh::runtime::Deps::get().sessionManager->rotateShareRefreshToken(session);
        return session->tokens->shareRefreshToken->rawToken;
    }

    std::string issueHumanRefresh(const std::shared_ptr<vh::protocols::ws::Session>& session) {
        vh::runtime::Deps::get().sessionManager->rotateRefreshToken(session);
        return session->tokens->refreshToken->rawToken;
    }
};

request previewRequest(const std::string& target) {
    request req{verb::get, target, 11};
    return req;
}

status responseStatus(const vh::protocols::http::model::preview::Response& response) {
    return std::visit([](const auto& res) { return res.result(); }, response);
}

std::string stringBody(const vh::protocols::http::model::preview::Response& response) {
    if (const auto* res = std::get_if<string_response>(&response)) return res->body();
    return {};
}

std::string vectorBody(const vh::protocols::http::model::preview::Response& response) {
    const auto* res = std::get_if<vector_response>(&response);
    if (!res) return {};
    return {res->body().begin(), res->body().end()};
}

TEST_F(HttpSharePreviewTest, RejectsMissingSessionCookie) {
    Router::setPreviewSessionResolverForTesting([](const request&) -> std::shared_ptr<vh::protocols::ws::Session> {
        throw std::runtime_error("missing session");
    });

    auto response = Router::handlePreview(previewRequest("/preview?share=1&path=%2Freport.jpg&size=128"));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_NE(std::string::npos, stringBody(response).find("missing session"));
}

TEST_F(HttpSharePreviewTest, SharePreviewReadsShareRefreshCookieAndIgnoresHumanRefreshCookie) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Preview));
    std::string shareRefresh;
    try {
        shareRefresh = issueShareRefresh(session);
    } catch (const std::exception& ex) {
        GTEST_SKIP() << "JWT secret store unavailable: " << ex.what();
    }
    installSharePreviewDependencies();

    auto req = previewRequest("/preview?share=1&path=%2Freport.jpg&size=" + std::to_string(previewSize));
    req.set(field::cookie, "refresh=not-a-share-token; share_refresh=" + shareRefresh);

    auto response = Router::handlePreview(std::move(req));

    EXPECT_EQ(status::ok, responseStatus(response));
    EXPECT_EQ("cached-thumbnail", vectorBody(response));
    EXPECT_FALSE(session->user);
}

TEST_F(HttpSharePreviewTest, SharePreviewRejectsMissingShareRefreshCookie) {
    auto response = Router::handlePreview(previewRequest("/preview?share=1&path=%2Freport.jpg&size=128"));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_NE(std::string::npos, stringBody(response).find("Share refresh token not set"));
}

TEST_F(HttpSharePreviewTest, SharePreviewRejectsPendingShareRefreshSession) {
    auto session = std::make_shared<vh::protocols::ws::Session>(std::make_shared<vh::protocols::ws::Router>());
    session->ipAddress = "127.0.0.1";
    session->userAgent = "http-share-preview-test";
    session->setPendingShareSession("share-session-1", "vhss_pending");
    std::string shareRefresh;
    try {
        shareRefresh = issueShareRefresh(session);
    } catch (const std::exception& ex) {
        GTEST_SKIP() << "JWT secret store unavailable: " << ex.what();
    }

    auto req = previewRequest("/preview?share=1&path=%2Freport.jpg&size=128");
    req.set(field::cookie, "share_refresh=" + shareRefresh);

    auto response = Router::handlePreview(std::move(req));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_FALSE(session->user);
}

TEST_F(HttpSharePreviewTest, SharePreviewDoesNotAcceptHumanRefreshCookie) {
    auto session = std::make_shared<vh::protocols::ws::Session>(std::make_shared<vh::protocols::ws::Router>());
    session->ipAddress = "127.0.0.1";
    session->userAgent = "http-share-preview-test";
    auto user = std::make_shared<vh::identities::User>();
    user->id = 9;
    user->name = "preview-human";

    std::string humanRefresh;
    try {
        humanRefresh = issueHumanRefresh(session);
        session->setAuthenticatedUser(user);
        vh::runtime::Deps::get().sessionManager->cache(session);
    } catch (const std::exception& ex) {
        GTEST_SKIP() << "JWT secret store unavailable: " << ex.what();
    }

    auto req = previewRequest("/preview?share=1&path=%2Freport.jpg&size=128");
    req.set(field::cookie, "refresh=" + humanRefresh);

    auto response = Router::handlePreview(std::move(req));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_NE(std::string::npos, stringBody(response).find("Share refresh token not set"));
}

TEST_F(HttpSharePreviewTest, NonSharePreviewDoesNotAcceptShareRefreshCookie) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Preview));
    std::string shareRefresh;
    try {
        shareRefresh = issueShareRefresh(session);
    } catch (const std::exception& ex) {
        GTEST_SKIP() << "JWT secret store unavailable: " << ex.what();
    }

    auto req = previewRequest("/preview?vault_id=42&path=%2Fshared%2Freport.jpg&size=128");
    req.set(field::cookie, "share_refresh=" + shareRefresh);

    auto response = Router::handlePreview(std::move(req));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_NE(std::string::npos, stringBody(response).find("Refresh token not set"));
}

TEST_F(HttpSharePreviewTest, RejectsPendingShareSession) {
    auto session = std::make_shared<vh::protocols::ws::Session>(std::make_shared<vh::protocols::ws::Router>());
    session->setPendingShareSession("share-session-1", "vhss_pending");
    installSharePreviewHooks(session);

    auto response = Router::handlePreview(previewRequest("/preview?share=1&path=%2Freport.jpg&size=128"));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_FALSE(session->user);
}

TEST_F(HttpSharePreviewTest, AllowsReadyShareSessionWithPreviewGrant) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Preview));
    installSharePreviewHooks(session);

    auto response = Router::handlePreview(previewRequest(
        "/preview?share=1&path=%2Freport.jpg&size=" + std::to_string(previewSize)));

    EXPECT_EQ(status::ok, responseStatus(response));
    EXPECT_EQ("cached-thumbnail", vectorBody(response));
    EXPECT_FALSE(session->user);
    ASSERT_TRUE(session->sharePrincipal());
    EXPECT_TRUE(session->sharePrincipal()->scoped_vault_role);
    ASSERT_FALSE(store->audits.empty());
    EXPECT_TRUE(std::ranges::any_of(store->audits, [](const auto& audit) {
        return audit && audit->event_type == "share.preview.http";
    }));
}

TEST_F(HttpSharePreviewTest, DeniesReadyShareSessionWithoutPreviewGrant) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Metadata));
    installSharePreviewHooks(session);

    auto response = Router::handlePreview(previewRequest(
        "/preview?share=1&path=%2Freport.jpg&size=" + std::to_string(previewSize)));

    EXPECT_EQ(status::bad_request, responseStatus(response));
    EXPECT_FALSE(session->user);
    EXPECT_NE(std::string::npos, stringBody(response).find("denied"));
}

TEST_F(HttpSharePreviewTest, DeniesTraversalOutsideShareScope) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Preview));
    installSharePreviewHooks(session);

    auto response = Router::handlePreview(previewRequest(
        "/preview?share=1&path=%2F..%2Fsecret.jpg&size=" + std::to_string(previewSize)));

    EXPECT_EQ(status::bad_request, responseStatus(response));
    EXPECT_FALSE(session->user);
    EXPECT_NE(std::string::npos, stringBody(response).find("escapes share root"));
}

}
