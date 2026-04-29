#pragma once

#include "db/model/ListQueryParams.hpp"
#include "rbac/Actor.hpp"
#include "share/Link.hpp"
#include "share/Principal.hpp"
#include "share/Scope.hpp"
#include "share/Session.hpp"

#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vh::share {

struct AuditEvent;
struct EmailChallenge;

struct AuthorizationDecision {
    bool allowed{false};
    std::string reason{"denied"};
};

class ShareAuthorizer {
public:
    virtual ~ShareAuthorizer() = default;

    [[nodiscard]] virtual AuthorizationDecision canCreateLink(const rbac::Actor& actor, const Link& link) const = 0;
    [[nodiscard]] virtual AuthorizationDecision canUpdateLink(const rbac::Actor& actor, const Link& existing, const Link& updated) const = 0;
    [[nodiscard]] virtual AuthorizationDecision canManageLink(const rbac::Actor& actor, const Link& link) const = 0;
    [[nodiscard]] virtual AuthorizationDecision canListVaultLinks(const rbac::Actor& actor, uint32_t vaultId) const = 0;
};

class ShareStore {
public:
    virtual ~ShareStore() = default;

    virtual std::shared_ptr<Link> createLink(const std::shared_ptr<Link>& link) = 0;
    virtual std::shared_ptr<Link> getLink(const std::string& id) = 0;
    virtual std::shared_ptr<Link> getLinkByLookupId(const std::string& lookupId) = 0;
    virtual std::vector<std::shared_ptr<Link>> listLinksForUser(uint32_t userId, const db::model::ListQueryParams& params) = 0;
    virtual std::vector<std::shared_ptr<Link>> listLinksForVault(uint32_t vaultId, const db::model::ListQueryParams& params) = 0;
    virtual std::shared_ptr<Link> updateLink(const std::shared_ptr<Link>& link) = 0;
    virtual void revokeLink(const std::string& id, uint32_t revokedBy) = 0;
    virtual void rotateLinkToken(
        const std::string& id,
        const std::string& lookupId,
        const std::vector<uint8_t>& tokenHash,
        uint32_t updatedBy
    ) = 0;
    virtual void touchLinkAccess(const std::string& id) = 0;

    virtual std::shared_ptr<Session> createSession(const std::shared_ptr<Session>& session) = 0;
    virtual std::shared_ptr<Session> getSession(const std::string& id) = 0;
    virtual std::shared_ptr<Session> getSessionByLookupId(const std::string& lookupId) = 0;
    virtual void verifySession(const std::string& sessionId, const std::vector<uint8_t>& emailHash) = 0;
    virtual void touchSession(const std::string& sessionId) = 0;
    virtual void revokeSession(const std::string& sessionId) = 0;
    virtual void revokeSessionsForShare(const std::string& shareId) = 0;

    virtual std::shared_ptr<EmailChallenge> createEmailChallenge(const std::shared_ptr<EmailChallenge>& challenge) = 0;
    virtual std::shared_ptr<EmailChallenge> getEmailChallenge(const std::string& id) = 0;
    virtual std::shared_ptr<EmailChallenge> getActiveEmailChallenge(
        const std::string& shareId,
        const std::vector<uint8_t>& emailHash
    ) = 0;
    virtual void recordEmailChallengeAttempt(const std::string& challengeId) = 0;
    virtual void consumeEmailChallenge(const std::string& challengeId) = 0;

    virtual void appendAuditEvent(const std::shared_ptr<AuditEvent>& event) = 0;
};

struct ManagerOptions {
    std::time_t session_ttl_seconds{3600};
    std::time_t challenge_ttl_seconds{900};
    uint32_t challenge_max_attempts{6};
    std::function<std::time_t()> clock{[] { return std::time(nullptr); }};
    std::function<std::string()> challenge_code_generator{};
};

struct CreateLinkRequest {
    Link link;
};

struct CreateLinkResult {
    std::shared_ptr<Link> link;
    std::string public_token;
    std::string public_url_path;
};

struct UpdateLinkRequest {
    std::shared_ptr<Link> link;
};

struct RotateLinkTokenResult {
    std::shared_ptr<Link> link;
    std::string public_token;
    std::string public_url_path;
};

enum class OpenSessionStatus { Ready, EmailRequired };

struct OpenSessionResult {
    OpenSessionStatus status{OpenSessionStatus::Ready};
    std::shared_ptr<Link> link;
    std::shared_ptr<Session> session;
    std::string session_token;

    [[nodiscard]] bool ready() const noexcept { return status == OpenSessionStatus::Ready; }
};

struct StartEmailChallengeRequest {
    std::optional<std::string> public_token;
    std::optional<std::string> session_token;
    std::string email;
    std::optional<std::string> ip_address;
    std::optional<std::string> user_agent;
};

struct StartEmailChallengeResult {
    std::shared_ptr<Link> link;
    std::shared_ptr<Session> session;
    std::shared_ptr<EmailChallenge> challenge;
    std::string session_token;
    std::string verification_code;
};

struct ConfirmEmailChallengeRequest {
    std::string challenge_id;
    std::string session_id;
    std::string code;
};

struct ConfirmEmailChallengeResult {
    std::shared_ptr<Session> session;
    bool verified{false};
};

class Manager {
public:
    Manager();
    Manager(
        std::shared_ptr<ShareStore> store,
        std::shared_ptr<ShareAuthorizer> authorizer,
        ManagerOptions options = {}
    );

    [[nodiscard]] CreateLinkResult createLink(const rbac::Actor& actor, CreateLinkRequest request);
    [[nodiscard]] std::shared_ptr<Link> getLinkForManagement(const rbac::Actor& actor, const std::string& id);
    [[nodiscard]] std::vector<std::shared_ptr<Link>> listLinksForUser(
        const rbac::Actor& actor,
        const db::model::ListQueryParams& params
    );
    [[nodiscard]] std::vector<std::shared_ptr<Link>> listLinksForVault(
        const rbac::Actor& actor,
        uint32_t vaultId,
        const db::model::ListQueryParams& params
    );
    [[nodiscard]] std::shared_ptr<Link> updateLink(const rbac::Actor& actor, UpdateLinkRequest request);
    void revokeLink(const rbac::Actor& actor, const std::string& id);
    [[nodiscard]] RotateLinkTokenResult rotateLinkToken(const rbac::Actor& actor, const std::string& id);

    [[nodiscard]] OpenSessionResult openPublicSession(
        std::string_view publicToken,
        std::optional<std::string> ipAddress = std::nullopt,
        std::optional<std::string> userAgent = std::nullopt
    );
    [[nodiscard]] StartEmailChallengeResult startEmailChallenge(StartEmailChallengeRequest request);
    [[nodiscard]] ConfirmEmailChallengeResult confirmEmailChallenge(ConfirmEmailChallengeRequest request);
    [[nodiscard]] std::shared_ptr<Principal> resolvePrincipal(
        std::string_view sessionToken,
        std::optional<std::string> ipAddress = std::nullopt,
        std::optional<std::string> userAgent = std::nullopt
    );
    [[nodiscard]] ScopeDecision authorize(
        const Principal& principal,
        Operation operation,
        std::string_view requestedPath,
        std::optional<TargetType> targetType = std::nullopt,
        std::optional<uint32_t> vaultId = std::nullopt
    );

private:
    std::shared_ptr<ShareStore> store_;
    std::shared_ptr<ShareAuthorizer> authorizer_;
    ManagerOptions options_;
};

}
