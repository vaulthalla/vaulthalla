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
#include <vector>
#include <variant>

namespace vh::protocols::http::test_share_preview {
constexpr std::time_t kNow = 1'800'000'000;
constexpr uint32_t kVaultId = 42;
constexpr uint32_t kRootEntryId = 10;

std::vector<uint8_t> tinyPng() {
    return {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c,
        0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41,
        0x54, 0x78, 0xda, 0x63, 0xfc, 0xff, 0x1f, 0x00,
        0x03, 0x03, 0x02, 0x00, 0xef, 0xbf, 0xa7, 0xdb,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
        0xae, 0x42, 0x60, 0x82
    };
}

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
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<vh::fs::model::Entry>>> children;

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

    std::vector<std::shared_ptr<vh::fs::model::Entry>> listChildren(uint32_t parentEntryId) override {
        if (!children.contains(parentEntryId)) return {};
        return children.at(parentEntryId);
    }
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
        provider->children[root->id].push_back(file);

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

std::string responseHeader(
    const vh::protocols::http::model::preview::Response& response,
    const field header
) {
    return std::visit([header](const auto& res) -> std::string {
        const auto it = res.find(header);
        return it == res.end() ? std::string{} : std::string(it->value());
    }, response);
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

TEST_F(HttpSharePreviewTest, SharePreviewRendersFallbackWhenThumbnailCacheIsMissing) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Preview));
    installSharePreviewHooks(session);

    std::filesystem::remove_all(engine->paths->thumbnailRoot / file->base32_alias);
    file->mime_type = "image/png";
    file->backing_path = testRoot / "backing" / "uploaded-image";
    std::filesystem::create_directories(file->backing_path.parent_path());
    const auto bytes = tinyPng();
    std::ofstream(file->backing_path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    auto response = Router::handlePreview(previewRequest(
        "/preview?share=1&path=%2Freport.jpg&size=" + std::to_string(previewSize)));

    EXPECT_EQ(status::ok, responseStatus(response));
    EXPECT_FALSE(vectorBody(response).empty());
    EXPECT_NE("cached-thumbnail", vectorBody(response));
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

TEST_F(HttpSharePreviewTest, HumanDownloadRejectsMissingRefreshCookie) {
    auto response = Router::handleDownload(previewRequest("/download?vault_id=42&path=%2Fshared%2Freport.jpg"));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_NE(std::string::npos, stringBody(response).find("Refresh token not set"));
}

TEST_F(HttpSharePreviewTest, ShareDownloadRejectsMissingShareRefreshCookie) {
    auto response = Router::handleDownload(previewRequest("/download?share=1&path=%2Freport.jpg"));

    EXPECT_EQ(status::unauthorized, responseStatus(response));
    EXPECT_NE(std::string::npos, stringBody(response).find("Share refresh token not set"));
}

TEST_F(HttpSharePreviewTest, ShareFileDownloadReadsShareRefreshCookieAndIgnoresHumanRefreshCookie) {
    file->size_bytes = 0;
    auto session = readySession(vh::share::bit(vh::share::Operation::Download));
    std::string shareRefresh;
    try {
        shareRefresh = issueShareRefresh(session);
    } catch (const std::exception& ex) {
        GTEST_SKIP() << "JWT secret store unavailable: " << ex.what();
    }
    installSharePreviewDependencies();

    auto req = previewRequest("/download?share=1&path=%2Freport.jpg");
    req.set(field::cookie, "refresh=not-a-share-token; share_refresh=" + shareRefresh);

    auto response = Router::handleDownload(std::move(req));

    EXPECT_EQ(status::ok, responseStatus(response));
    EXPECT_EQ("", vectorBody(response));
    EXPECT_EQ("attachment; filename=\"report.jpg\"", responseHeader(response, field::content_disposition));
    EXPECT_FALSE(session->user);
    ASSERT_FALSE(store->audits.empty());
    EXPECT_TRUE(std::ranges::any_of(store->audits, [](const auto& audit) {
        return audit && audit->event_type == "share.download.http";
    }));
}

TEST_F(HttpSharePreviewTest, ShareFileDownloadDeniesMissingGrant) {
    file->size_bytes = 0;
    auto session = readySession(vh::share::bit(vh::share::Operation::Metadata));
    installSharePreviewHooks(session);

    auto response = Router::handleDownload(previewRequest("/download?share=1&path=%2Freport.jpg"));

    EXPECT_EQ(status::forbidden, responseStatus(response));
    EXPECT_FALSE(session->user);
}

TEST_F(HttpSharePreviewTest, ShareDownloadDeniesTraversalOutsideScope) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Download));
    installSharePreviewHooks(session);

    auto response = Router::handleDownload(previewRequest("/download?share=1&path=%2F..%2Fsecret.txt"));

    EXPECT_EQ(status::bad_request, responseStatus(response));
    EXPECT_FALSE(session->user);
    EXPECT_NE(std::string::npos, stringBody(response).find("escapes share root"));
}

TEST_F(HttpSharePreviewTest, ShareDirectoryArchiveRequiresListGrant) {
    auto session = readySession(vh::share::bit(vh::share::Operation::Download));
    installSharePreviewHooks(session);

    auto response = Router::handleDownload(previewRequest("/download?share=1&path=%2F"));

    EXPECT_EQ(status::forbidden, responseStatus(response));
    EXPECT_FALSE(session->user);
}

TEST_F(HttpSharePreviewTest, ShareDirectoryArchiveContainsScopedRelativeEntries) {
    file->size_bytes = 0;

    auto nested = std::make_shared<vh::fs::model::Directory>();
    nested->id = 12;
    nested->parent_id = static_cast<int32_t>(kRootEntryId);
    nested->name = "nested";
    nested->vault_id = static_cast<int32_t>(kVaultId);
    nested->path = "/shared/nested";

    auto nestedFile = std::make_shared<vh::fs::model::File>();
    nestedFile->id = 13;
    nestedFile->parent_id = static_cast<int32_t>(nested->id);
    nestedFile->name = "empty.txt";
    nestedFile->vault_id = static_cast<int32_t>(kVaultId);
    nestedFile->path = "/shared/nested/empty.txt";
    nestedFile->mime_type = "text/plain";
    nestedFile->size_bytes = 0;

    provider->byId[nested->id] = nested;
    provider->byPath[nested->path.string()] = nested;
    provider->byId[nestedFile->id] = nestedFile;
    provider->byPath[nestedFile->path.string()] = nestedFile;
    provider->children[kRootEntryId].push_back(nested);
    provider->children[nested->id].push_back(nestedFile);

    auto outside = std::make_shared<vh::fs::model::File>();
    outside->id = 14;
    outside->name = "secret.txt";
    outside->vault_id = static_cast<int32_t>(kVaultId);
    outside->path = "/secret.txt";
    outside->size_bytes = 0;
    provider->byId[outside->id] = outside;
    provider->byPath[outside->path.string()] = outside;

    auto session = readySession(vh::share::bit(vh::share::Operation::Download) | vh::share::bit(vh::share::Operation::List));
    installSharePreviewHooks(session);

    auto response = Router::handleDownload(previewRequest("/download?share=1&path=%2F"));
    const auto archive = vectorBody(response);

    EXPECT_EQ(status::ok, responseStatus(response));
    EXPECT_EQ("application/zip", responseHeader(response, field::content_type));
    EXPECT_NE(std::string::npos, archive.find("report.jpg"));
    EXPECT_NE(std::string::npos, archive.find("nested/"));
    EXPECT_NE(std::string::npos, archive.find("nested/empty.txt"));
    EXPECT_EQ(std::string::npos, archive.find("/secret.txt"));
    EXPECT_EQ(std::string::npos, archive.find(".."));
    EXPECT_EQ("attachment; filename=\"shared.zip\"", responseHeader(response, field::content_disposition));
}

TEST_F(HttpSharePreviewTest, ShareDownloadSanitizesContentDispositionFilename) {
    file->size_bytes = 0;
    file->name = "bad\";name.txt";
    auto session = readySession(vh::share::bit(vh::share::Operation::Download));
    installSharePreviewHooks(session);

    auto response = Router::handleDownload(previewRequest("/download?share=1&path=%2Freport.jpg"));

    EXPECT_EQ(status::ok, responseStatus(response));
    EXPECT_EQ("attachment; filename=\"bad__name.txt\"", responseHeader(response, field::content_disposition));
}

}
