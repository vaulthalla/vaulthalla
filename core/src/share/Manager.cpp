#include "share/Manager.hpp"

#include "db/query/share/AuditEvent.hpp"
#include "db/query/share/EmailChallenge.hpp"
#include "db/query/share/Link.hpp"
#include "db/query/share/Session.hpp"
#include "identities/User.hpp"
#include "rbac/permission/vault/Filesystem.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "share/AuditEvent.hpp"
#include "share/EmailChallenge.hpp"
#include "share/PrincipalResolver.hpp"
#include "share/Token.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace vh::share {
namespace {
using FilesystemAction = rbac::permission::vault::FilesystemAction;

constexpr std::string_view kSessionOpen = "share.session.open";
constexpr std::string_view kChallengeCreated = "share.email.challenge.created";
constexpr std::string_view kChallengeConfirm = "share.email.challenge.confirm";
constexpr std::string_view kPrincipalResolve = "share.principal.resolve";
constexpr std::string_view kScopeAuthorize = "share.scope.authorize";

class QueryShareStore final : public ShareStore {
public:
    std::shared_ptr<Link> createLink(const std::shared_ptr<Link>& link) override {
        return db::query::share::Link::create(link);
    }

    std::shared_ptr<Link> getLink(const std::string& id) override { return db::query::share::Link::get(id); }

    std::shared_ptr<Link> getLinkByLookupId(const std::string& lookupId) override {
        return db::query::share::Link::getByLookupId(lookupId);
    }

    std::vector<std::shared_ptr<Link>> listLinksForUser(
        const uint32_t userId,
        const db::model::ListQueryParams& params
    ) override {
        return db::query::share::Link::listForUser(userId, params);
    }

    std::vector<std::shared_ptr<Link>> listLinksForVault(
        const uint32_t vaultId,
        const db::model::ListQueryParams& params
    ) override {
        return db::query::share::Link::listForVault(vaultId, params);
    }

    std::shared_ptr<Link> updateLink(const std::shared_ptr<Link>& link) override {
        return db::query::share::Link::update(link);
    }

    void revokeLink(const std::string& id, const uint32_t revokedBy) override {
        db::query::share::Link::revoke(id, revokedBy);
    }

    void rotateLinkToken(
        const std::string& id,
        const std::string& lookupId,
        const std::vector<uint8_t>& tokenHash,
        const uint32_t updatedBy
    ) override {
        db::query::share::Link::rotateToken(id, lookupId, tokenHash, updatedBy);
    }

    void touchLinkAccess(const std::string& id) override { db::query::share::Link::touchAccess(id); }

    std::shared_ptr<Session> createSession(const std::shared_ptr<Session>& session) override {
        return db::query::share::Session::create(session);
    }

    std::shared_ptr<Session> getSession(const std::string& id) override {
        return db::query::share::Session::get(id);
    }

    std::shared_ptr<Session> getSessionByLookupId(const std::string& lookupId) override {
        return db::query::share::Session::getByLookupId(lookupId);
    }

    void verifySession(const std::string& sessionId, const std::vector<uint8_t>& emailHash) override {
        db::query::share::Session::verify(sessionId, emailHash);
    }

    void touchSession(const std::string& sessionId) override { db::query::share::Session::touch(sessionId); }

    void revokeSession(const std::string& sessionId) override { db::query::share::Session::revoke(sessionId); }

    void revokeSessionsForShare(const std::string& shareId) override {
        db::query::share::Session::revokeForShare(shareId);
    }

    std::shared_ptr<EmailChallenge> createEmailChallenge(
        const std::shared_ptr<EmailChallenge>& challenge
    ) override {
        return db::query::share::EmailChallenge::create(challenge);
    }

    std::shared_ptr<EmailChallenge> getEmailChallenge(const std::string& id) override {
        return db::query::share::EmailChallenge::get(id);
    }

    std::shared_ptr<EmailChallenge> getActiveEmailChallenge(
        const std::string& shareId,
        const std::vector<uint8_t>& emailHash
    ) override {
        return db::query::share::EmailChallenge::getActive(shareId, emailHash);
    }

    void recordEmailChallengeAttempt(const std::string& challengeId) override {
        db::query::share::EmailChallenge::recordAttempt(challengeId);
    }

    void consumeEmailChallenge(const std::string& challengeId) override {
        db::query::share::EmailChallenge::consume(challengeId);
    }

    void appendAuditEvent(const std::shared_ptr<AuditEvent>& event) override {
        db::query::share::AuditEvent::append(event);
    }
};

[[nodiscard]] AuthorizationDecision allow() { return {true, "allowed"}; }
[[nodiscard]] AuthorizationDecision deny(std::string reason) { return {false, std::move(reason)}; }

[[nodiscard]] std::shared_ptr<identities::User> humanUser(const rbac::Actor& actor) {
    if (!actor.isHuman() || !actor.canUseHumanPrivileges()) return nullptr;
    return actor.user();
}

[[nodiscard]] bool hasFsPermission(
    const std::shared_ptr<identities::User>& user,
    const uint32_t vaultId,
    const std::string& path,
    const FilesystemAction action
) {
    return rbac::resolver::Vault::has<FilesystemAction>({
        .user = user,
        .permission = action,
        .vault_id = vaultId,
        .path = std::filesystem::path{Scope::normalizeVaultPath(path)}
    });
}

[[nodiscard]] bool hasMetadataPermission(
    const std::shared_ptr<identities::User>& user,
    const uint32_t vaultId,
    const std::string& path
) {
    return rbac::resolver::Vault::hasAny(
        rbac::resolver::vault::Context<FilesystemAction>{
            .user = user,
            .permission = FilesystemAction::Lookup,
            .vault_id = vaultId,
            .path = std::filesystem::path{Scope::normalizeVaultPath(path)}
        },
        rbac::resolver::vault::Context<FilesystemAction>{
            .user = user,
            .permission = FilesystemAction::Preview,
            .vault_id = vaultId,
            .path = std::filesystem::path{Scope::normalizeVaultPath(path)}
        }
    );
}

[[nodiscard]] bool forEachOperation(const uint32_t ops, const std::function<bool(Operation)>& fn) {
    constexpr Operation all[] = {
        Operation::Metadata,
        Operation::List,
        Operation::Preview,
        Operation::Download,
        Operation::Upload,
        Operation::Mkdir,
        Operation::Overwrite
    };

    for (const auto op : all)
        if ((ops & bit(op)) != 0 && !fn(op)) return false;
    return true;
}

class DefaultShareAuthorizer final : public ShareAuthorizer {
public:
    AuthorizationDecision canCreateLink(const rbac::Actor& actor, const Link& link) const override {
        return canGrant(actor, link);
    }

    AuthorizationDecision canUpdateLink(
        const rbac::Actor& actor,
        const Link& existing,
        const Link& updated
    ) const override {
        if (const auto user = humanUser(actor); user && user->id == existing.created_by)
            return canGrant(actor, updated);
        return canGrant(actor, updated);
    }

    AuthorizationDecision canManageLink(const rbac::Actor& actor, const Link& link) const override {
        const auto user = humanUser(actor);
        if (!user) return deny("share_management_requires_human_user");
        if (user->id == link.created_by) return allow();
        return hasShareMode(actor, link);
    }

    AuthorizationDecision canListVaultLinks(const rbac::Actor& actor, const uint32_t vaultId) const override {
        const auto user = humanUser(actor);
        if (!user) return deny("share_management_requires_human_user");
        if (hasFsPermission(user, vaultId, "/", FilesystemAction::SharePublic) ||
            hasFsPermission(user, vaultId, "/", FilesystemAction::SharePublicValidated))
            return allow();
        return deny("missing_share_management_permission");
    }

private:
    [[nodiscard]] AuthorizationDecision hasShareMode(const rbac::Actor& actor, const Link& link) const {
        const auto user = humanUser(actor);
        if (!user) return deny("share_management_requires_human_user");

        const auto required = link.access_mode == AccessMode::EmailValidated
                                  ? FilesystemAction::SharePublicValidated
                                  : FilesystemAction::SharePublic;
        if (!hasFsPermission(user, link.vault_id, link.root_path, required))
            return deny(link.access_mode == AccessMode::EmailValidated
                            ? "missing_share_public_validated_permission"
                            : "missing_share_public_permission");

        return allow();
    }

    [[nodiscard]] AuthorizationDecision canGrant(const rbac::Actor& actor, const Link& link) const {
        if (auto mode = hasShareMode(actor, link); !mode.allowed) return mode;

        const auto user = humanUser(actor);
        if (!user) return deny("share_management_requires_human_user");

        const bool ok = forEachOperation(link.allowed_ops, [&](const Operation op) {
            switch (op) {
                case Operation::Metadata: return hasMetadataPermission(user, link.vault_id, link.root_path);
                case Operation::List: return hasFsPermission(user, link.vault_id, link.root_path, FilesystemAction::List);
                case Operation::Preview: return hasFsPermission(user, link.vault_id, link.root_path, FilesystemAction::Preview);
                case Operation::Download: return hasFsPermission(user, link.vault_id, link.root_path, FilesystemAction::Read);
                case Operation::Upload: return hasFsPermission(user, link.vault_id, link.root_path, FilesystemAction::Write);
                case Operation::Mkdir: return hasFsPermission(user, link.vault_id, link.root_path, FilesystemAction::Touch);
                case Operation::Overwrite: return hasFsPermission(user, link.vault_id, link.root_path, FilesystemAction::Overwrite);
            }
            return false;
        });

        if (!ok) return deny("delegated_operation_not_grantable");
        return allow();
    }
};

[[nodiscard]] std::shared_ptr<AuditEvent> auditEvent(
    std::string eventType,
    const AuditStatus status,
    const std::optional<std::string>& shareId = std::nullopt,
    const std::optional<std::string>& sessionId = std::nullopt
) {
    auto event = std::make_shared<AuditEvent>();
    event->share_id = shareId;
    event->share_session_id = sessionId;
    event->actor_type = AuditActorType::System;
    event->event_type = std::move(eventType);
    event->status = status;
    return event;
}

void attachHuman(AuditEvent& event, const rbac::Actor& actor) {
    if (const auto user = humanUser(actor)) {
        event.actor_type = AuditActorType::OwnerUser;
        event.actor_user_id = user->id;
    }
}

void attachLink(AuditEvent& event, const Link& link) {
    event.share_id = link.id.empty() ? std::nullopt : std::make_optional(link.id);
    event.vault_id = link.vault_id;
    event.target_entry_id = link.root_entry_id;
    event.target_path = link.root_path;
}

void attachPrincipal(AuditEvent& event, const Principal& principal) {
    event.share_id = principal.share_id;
    event.share_session_id = principal.share_session_id;
    event.actor_type = AuditActorType::SharePrincipal;
    event.vault_id = principal.vault_id;
    event.target_entry_id = principal.root_entry_id;
}

void setDenied(AuditEvent& event, std::string code, std::string message = {}) {
    event.status = AuditStatus::Denied;
    event.error_code = std::move(code);
    if (!message.empty()) event.error_message = std::move(message);
}

[[nodiscard]] std::time_t now(const ManagerOptions& options) {
    if (!options.clock) throw std::runtime_error("Share manager clock is unavailable");
    return options.clock();
}

[[nodiscard]] std::string challengeCode(const ManagerOptions& options) {
    return options.challenge_code_generator ? options.challenge_code_generator() : EmailChallenge::generateCode();
}

void requireAllowed(const AuthorizationDecision& decision, const std::string_view operation) {
    if (!decision.allowed)
        throw std::runtime_error("Share " + std::string(operation) + " denied: " + decision.reason);
}

template<typename T>
[[nodiscard]] std::shared_ptr<T> requirePtr(const std::shared_ptr<T>& value, const std::string_view reason) {
    if (!value) throw std::runtime_error(std::string(reason));
    return value;
}

void normalizeAndValidate(Link& link) {
    link.root_path = Scope::normalizeVaultPath(link.root_path);
    link.grant().requireValid();
}

[[nodiscard]] std::shared_ptr<Link> linkFromPublicToken(
    ShareStore& store,
    const std::string_view publicToken,
    const std::time_t current
) {
    const auto parsed = Token::parse(publicToken);
    if (parsed.kind != TokenKind::PublicShare) throw std::invalid_argument("Expected public share token");

    auto link = store.getLinkByLookupId(parsed.lookup_id);
    if (!link || !Token::verify(link->token_hash, TokenKind::PublicShare, parsed.secret))
        throw std::runtime_error("Invalid public share token");
    if (!link->isActive(current)) throw std::runtime_error("Share link is inactive");

    return link;
}

[[nodiscard]] std::pair<std::shared_ptr<Session>, std::string> createSessionForLink(
    ShareStore& store,
    const Link& link,
    const ManagerOptions& options,
    const std::optional<std::string>& ipAddress,
    const std::optional<std::string>& userAgent
) {
    const auto token = Token::generate(TokenKind::ShareSession);
    auto session = std::make_shared<Session>();
    session->share_id = link.id;
    session->session_token_lookup_id = token.lookup_id;
    session->session_token_hash = token.hash;
    session->expires_at = now(options) + options.session_ttl_seconds;
    session->ip_address = ipAddress;
    session->user_agent = userAgent;
    return {store.createSession(session), token.raw};
}

[[nodiscard]] std::shared_ptr<Session> sessionFromToken(
    ShareStore& store,
    const std::string_view sessionToken,
    const std::time_t current
) {
    const auto parsed = Token::parse(sessionToken);
    if (parsed.kind != TokenKind::ShareSession) throw std::invalid_argument("Expected share session token");

    auto session = store.getSessionByLookupId(parsed.lookup_id);
    if (!session || !Token::verify(session->session_token_hash, TokenKind::ShareSession, parsed.secret))
        throw std::runtime_error("Invalid share session token");
    if (!session->isActive(current)) throw std::runtime_error("Share session is inactive");

    return session;
}
}

Manager::Manager()
    : Manager(std::make_shared<QueryShareStore>(), std::make_shared<DefaultShareAuthorizer>(), {}) {}

Manager::Manager(
    std::shared_ptr<ShareStore> store,
    std::shared_ptr<ShareAuthorizer> authorizer,
    ManagerOptions options
) : store_(std::move(store)), authorizer_(std::move(authorizer)), options_(std::move(options)) {
    if (!store_) throw std::invalid_argument("Share manager store is required");
    if (!authorizer_) throw std::invalid_argument("Share manager authorizer is required");
}

CreateLinkResult Manager::createLink(const rbac::Actor& actor, CreateLinkRequest request) {
    auto link = std::make_shared<Link>(std::move(request.link));
    if (const auto user = humanUser(actor)) link->created_by = user->id;

    normalizeAndValidate(*link);

    auto audit = auditEvent("share.link.create", AuditStatus::Success);
    attachHuman(*audit, actor);
    attachLink(*audit, *link);

    try {
        requireAllowed(authorizer_->canCreateLink(actor, *link), "create");
        const auto token = Token::generate(TokenKind::PublicShare);
        link->token_lookup_id = token.lookup_id;
        link->token_hash = token.hash;
        auto created = store_->createLink(link);
        attachLink(*audit, *created);
        store_->appendAuditEvent(audit);
        return {
            .link = created,
            .public_token = token.raw,
            .public_url_path = "/share/" + token.raw
        };
    } catch (...) {
        setDenied(*audit, "share_create_denied");
        store_->appendAuditEvent(audit);
        throw;
    }
}

std::shared_ptr<Link> Manager::getLinkForManagement(const rbac::Actor& actor, const std::string& id) {
    auto link = requirePtr(store_->getLink(id), "Share link not found");
    requireAllowed(authorizer_->canManageLink(actor, *link), "get");
    return link;
}

std::vector<std::shared_ptr<Link>> Manager::listLinksForUser(
    const rbac::Actor& actor,
    const db::model::ListQueryParams& params
) {
    const auto user = humanUser(actor);
    if (!user) throw std::runtime_error("Share list denied: share_management_requires_human_user");
    return store_->listLinksForUser(user->id, params);
}

std::vector<std::shared_ptr<Link>> Manager::listLinksForVault(
    const rbac::Actor& actor,
    const uint32_t vaultId,
    const db::model::ListQueryParams& params
) {
    requireAllowed(authorizer_->canListVaultLinks(actor, vaultId), "list vault");
    return store_->listLinksForVault(vaultId, params);
}

std::shared_ptr<Link> Manager::updateLink(const rbac::Actor& actor, UpdateLinkRequest request) {
    if (!request.link) throw std::invalid_argument("Share link update payload is required");
    auto existing = requirePtr(store_->getLink(request.link->id), "Share link not found");
    auto updated = std::make_shared<Link>(*request.link);

    updated->id = existing->id;
    updated->token_lookup_id = existing->token_lookup_id;
    updated->token_hash = existing->token_hash;
    updated->created_by = existing->created_by;
    updated->vault_id = existing->vault_id;
    updated->root_entry_id = existing->root_entry_id;
    updated->root_path = existing->root_path;
    updated->target_type = existing->target_type;
    if (const auto user = humanUser(actor)) updated->updated_by = user->id;

    normalizeAndValidate(*updated);

    auto audit = auditEvent("share.link.update", AuditStatus::Success, existing->id);
    attachHuman(*audit, actor);
    attachLink(*audit, *existing);

    try {
        requireAllowed(authorizer_->canUpdateLink(actor, *existing, *updated), "update");
        auto saved = store_->updateLink(updated);
        store_->appendAuditEvent(audit);
        return saved;
    } catch (...) {
        setDenied(*audit, "share_update_denied");
        store_->appendAuditEvent(audit);
        throw;
    }
}

void Manager::revokeLink(const rbac::Actor& actor, const std::string& id) {
    auto link = getLinkForManagement(actor, id);
    auto audit = auditEvent("share.link.revoke", AuditStatus::Success, link->id);
    attachHuman(*audit, actor);
    attachLink(*audit, *link);

    const auto user = humanUser(actor);
    store_->revokeLink(link->id, user ? user->id : 0);
    store_->revokeSessionsForShare(link->id);
    store_->appendAuditEvent(audit);
}

RotateLinkTokenResult Manager::rotateLinkToken(const rbac::Actor& actor, const std::string& id) {
    auto link = getLinkForManagement(actor, id);
    auto audit = auditEvent("share.link.rotate_token", AuditStatus::Success, link->id);
    attachHuman(*audit, actor);
    attachLink(*audit, *link);

    const auto user = humanUser(actor);
    const auto token = Token::generate(TokenKind::PublicShare);
    store_->rotateLinkToken(link->id, token.lookup_id, token.hash, user ? user->id : 0);
    store_->revokeSessionsForShare(link->id);
    auto rotated = requirePtr(store_->getLink(link->id), "Share link not found after token rotation");
    store_->appendAuditEvent(audit);

    return {
        .link = rotated,
        .public_token = token.raw,
        .public_url_path = "/share/" + token.raw
    };
}

OpenSessionResult Manager::openPublicSession(
    const std::string_view publicToken,
    const std::optional<std::string> ipAddress,
    const std::optional<std::string> userAgent
) {
    auto audit = auditEvent(std::string(kSessionOpen), AuditStatus::Success);
    audit->ip_address = ipAddress;
    audit->user_agent = userAgent;

    try {
        auto link = linkFromPublicToken(*store_, publicToken, now(options_));
        attachLink(*audit, *link);
        const auto [session, rawSessionToken] = createSessionForLink(*store_, *link, options_, ipAddress, userAgent);
        audit->share_id = link->id;
        audit->share_session_id = session->id;
        audit->actor_type = AuditActorType::SharePrincipal;
        store_->touchLinkAccess(link->id);
        store_->appendAuditEvent(audit);
        return {
            .status = link->requiresEmail() ? OpenSessionStatus::EmailRequired : OpenSessionStatus::Ready,
            .link = link,
            .session = session,
            .session_token = rawSessionToken
        };
    } catch (...) {
        setDenied(*audit, "share_session_open_denied");
        store_->appendAuditEvent(audit);
        throw;
    }
}

StartEmailChallengeResult Manager::startEmailChallenge(StartEmailChallengeRequest request) {
    if (!request.public_token && !request.session_token)
        throw std::invalid_argument("Public token or share session token is required");

    std::shared_ptr<Link> link;
    std::shared_ptr<Session> session;
    std::string rawSessionToken;

    if (request.session_token) {
        session = sessionFromToken(*store_, *request.session_token, now(options_));
        link = requirePtr(store_->getLink(session->share_id), "Share link not found");
    } else {
        link = linkFromPublicToken(*store_, *request.public_token, now(options_));
        auto created = createSessionForLink(*store_, *link, options_, request.ip_address, request.user_agent);
        session = std::move(created.first);
        rawSessionToken = std::move(created.second);

        auto sessionAudit = auditEvent(std::string(kSessionOpen), AuditStatus::Success, link->id, session->id);
        sessionAudit->actor_type = AuditActorType::SharePrincipal;
        sessionAudit->ip_address = request.ip_address;
        sessionAudit->user_agent = request.user_agent;
        attachLink(*sessionAudit, *link);
        store_->appendAuditEvent(sessionAudit);
    }

    if (!link->requiresEmail()) throw std::runtime_error("Share link does not require email verification");
    if (!link->isActive(now(options_))) throw std::runtime_error("Share link is inactive");
    if (!session->isActive(now(options_))) throw std::runtime_error("Share session is inactive");

    const auto normalizedEmail = EmailChallenge::normalizeEmail(request.email);
    const auto emailHash = EmailChallenge::hashEmail(normalizedEmail);
    const auto code = challengeCode(options_);

    auto challenge = std::make_shared<EmailChallenge>();
    challenge->share_id = link->id;
    challenge->share_session_id = session->id;
    challenge->email_hash = emailHash;
    challenge->code_hash = EmailChallenge::hashCode(code);
    challenge->max_attempts = options_.challenge_max_attempts;
    challenge->expires_at = now(options_) + options_.challenge_ttl_seconds;
    challenge->ip_address = request.ip_address;
    challenge->user_agent = request.user_agent;

    auto created = store_->createEmailChallenge(challenge);
    auto audit = auditEvent(std::string(kChallengeCreated), AuditStatus::Success, link->id, session->id);
    audit->actor_type = AuditActorType::SharePrincipal;
    audit->ip_address = request.ip_address;
    audit->user_agent = request.user_agent;
    attachLink(*audit, *link);
    store_->appendAuditEvent(audit);

    return {
        .link = link,
        .session = session,
        .challenge = created,
        .session_token = rawSessionToken,
        .verification_code = code
    };
}

ConfirmEmailChallengeResult Manager::confirmEmailChallenge(ConfirmEmailChallengeRequest request) {
    auto loadedChallenge = store_->getEmailChallenge(request.challenge_id);
    if (!loadedChallenge) throw std::runtime_error("Share email challenge not found");

    auto loadedSession = store_->getSession(request.session_id);
    if (!loadedSession) throw std::runtime_error("Share session not found");

    auto link = requirePtr(store_->getLink(loadedChallenge->share_id), "Share link not found");
    auto audit = auditEvent(std::string(kChallengeConfirm), AuditStatus::Success, link->id, loadedSession->id);
    audit->actor_type = AuditActorType::SharePrincipal;
    audit->ip_address = loadedChallenge->ip_address;
    audit->user_agent = loadedChallenge->user_agent;
    attachLink(*audit, *link);

    try {
        if (!link->requiresEmail()) throw std::runtime_error("Share link does not require email verification");
        if (!link->isActive(now(options_))) throw std::runtime_error("Share link is inactive");
        if (!loadedSession->isActive(now(options_))) throw std::runtime_error("Share session is inactive");
        if (loadedChallenge->share_id != loadedSession->share_id)
            throw std::runtime_error("Share email challenge does not belong to session share");
        if (loadedChallenge->share_session_id && *loadedChallenge->share_session_id != loadedSession->id)
            throw std::runtime_error("Share email challenge session mismatch");
        if (!loadedChallenge->canAttempt(now(options_)))
            throw std::runtime_error("Share email challenge cannot be attempted");

        if (!EmailChallenge::verifyCode(loadedChallenge->code_hash, request.code)) {
            store_->recordEmailChallengeAttempt(loadedChallenge->id);
            setDenied(*audit, "share_email_code_invalid");
            throw std::runtime_error("Share email challenge code is invalid");
        }

        store_->consumeEmailChallenge(loadedChallenge->id);
        store_->verifySession(loadedSession->id, loadedChallenge->email_hash);
        auto reloaded = store_->getSession(loadedSession->id);
        if (!reloaded) throw std::runtime_error("Share session not found after verification");
        store_->appendAuditEvent(audit);
        return {.session = reloaded, .verified = true};
    } catch (...) {
        if (!audit->error_code) setDenied(*audit, "share_email_verification_denied");
        store_->appendAuditEvent(audit);
        throw;
    }
}

std::shared_ptr<Principal> Manager::resolvePrincipal(
    const std::string_view sessionToken,
    const std::optional<std::string> ipAddress,
    const std::optional<std::string> userAgent
) {
    auto audit = auditEvent(std::string(kPrincipalResolve), AuditStatus::Success);
    audit->ip_address = ipAddress;
    audit->user_agent = userAgent;

    try {
        auto session = sessionFromToken(*store_, sessionToken, now(options_));
        audit->share_session_id = session->id;
        auto link = requirePtr(store_->getLink(session->share_id), "Share link not found");
        attachLink(*audit, *link);
        auto principal = PrincipalResolver::resolve(*link, *session, now(options_));
        store_->touchSession(session->id);
        store_->appendAuditEvent(audit);
        return principal;
    } catch (...) {
        setDenied(*audit, "share_principal_resolution_denied");
        store_->appendAuditEvent(audit);
        throw;
    }
}

ScopeDecision Manager::authorize(
    const Principal& principal,
    const Operation operation,
    const std::string_view requestedPath,
    const std::optional<TargetType> targetType,
    const std::optional<uint32_t> vaultId
) {
    auto audit = auditEvent(std::string(kScopeAuthorize), AuditStatus::Success, principal.share_id, principal.share_session_id);
    attachPrincipal(*audit, principal);
    audit->target_path = std::string(requestedPath);

    try {
        auto decision = Scope::authorize(principal, {
            .vault_id = vaultId.value_or(principal.vault_id),
            .path = std::string(requestedPath),
            .operation = operation,
            .target_type = targetType
        });
        audit->target_path = decision.normalized_path;
        if (!decision.allowed) setDenied(*audit, "share_scope_denied", decision.reason);
        store_->appendAuditEvent(audit);
        return decision;
    } catch (...) {
        setDenied(*audit, "share_scope_denied");
        store_->appendAuditEvent(audit);
        throw;
    }
}

}
