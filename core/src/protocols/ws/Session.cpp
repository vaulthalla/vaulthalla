#include "protocols/ws/Session.hpp"

#include "auth/Manager.hpp"
#include "auth/model/RefreshToken.hpp"
#include "auth/model/TokenPair.hpp"
#include "log/Registry.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/handler/fs/Upload.hpp"
#include "runtime/Deps.hpp"
#include "identities/User.hpp"
#include "protocols/cookie.hpp"
#include "rbac/Actor.hpp"
#include "share/Principal.hpp"

#include <boost/beast/http.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/asio.hpp>
#include <cstddef>
#include <string_view>

#include "config/Registry.hpp"

namespace {
namespace beast      = boost::beast;
namespace beast_http = boost::beast::http;
namespace websocket  = beast::websocket;
namespace asio       = boost::asio;

constexpr std::size_t kWebSocketReadBufferMaxBytes = 1024u * 1024u;

bool isShareHandshakeTarget(const std::string_view target) {
    constexpr std::string_view sharePath{"/ws/share"};
    return target == sharePath ||
           (target.starts_with(sharePath) && target.size() > sharePath.size() && target[sharePath.size()] == '?');
}
} // namespace

using namespace vh::identities;
using namespace vh::protocols;

namespace vh::protocols::ws {

Session::Session(const std::shared_ptr<Router>& router)
    : tokens(std::make_shared<auth::model::TokenPair>()), router_(router) {
    buffer_.max_size(kWebSocketReadBufferMaxBytes);
}

Session::~Session() {
    close();
    log::Registry::ws()->debug("[ws::Session] Session destroyed for IP: {}", getIPAddress());
}

std::string Session::getIPAddress() const {
    try {
        if (!ws_ || !ws_->next_layer().is_open()) return "unknown";
        return ws_->next_layer().remote_endpoint().address().to_string();
    } catch (...) {
        return "unknown";
    }
}

std::string Session::getUserAgent() const {
    if (const auto it = handshakeRequest_.find(beast_http::field::user_agent);
        it != handshakeRequest_.end()) return std::string(it->value());
    return  "unknown";
}

void Session::setAuthenticatedUser(const std::shared_ptr<User>& u) {
    clearShareSession();
    user = u;
    mode_ = SessionMode::Human;
    if (!tokens->refreshToken) throw std::runtime_error("Cannot set authenticated user without a refresh token in session");
    tokens->refreshToken->userId = user->id;
}

void Session::setPendingShareSession(std::string sessionId, std::string sessionToken) {
    if (user) throw std::runtime_error("Cannot attach share session to authenticated user session");
    if (sessionId.empty()) throw std::invalid_argument("Share session id is required");
    if (sessionToken.empty()) throw std::invalid_argument("Share session token is required");

    sharePrincipal_.reset();
    shareSessionId_ = std::move(sessionId);
    shareSessionToken_ = std::move(sessionToken);
    mode_ = SessionMode::SharePending;
}

void Session::setSharePrincipal(std::shared_ptr<vh::share::Principal> principal, std::string sessionToken) {
    if (user) throw std::runtime_error("Cannot attach share principal to authenticated user session");
    if (!principal) throw std::invalid_argument("Share principal is required");
    if (sessionToken.empty()) throw std::invalid_argument("Share session token is required");

    shareSessionId_ = principal->share_session_id;
    shareSessionToken_ = std::move(sessionToken);
    sharePrincipal_ = std::move(principal);
    mode_ = SessionMode::Share;
}

void Session::clearShareSession() {
    sharePrincipal_.reset();
    shareSessionId_.clear();
    shareSessionToken_.clear();
    mode_ = user ? SessionMode::Human : SessionMode::Unauthenticated;
}

void Session::setHandshakeRequest(const RequestType& req) { handshakeRequest_ = req; }

std::shared_ptr<handler::fs::Upload> Session::getUploadHandler() {
    if (!uploadHandler_) uploadHandler_ = std::make_shared<handler::fs::Upload>(shared_from_this());
    return uploadHandler_;
}

vh::rbac::Actor Session::rbacActor() const {
    if (user) return vh::rbac::Actor::human(user);
    if (mode_ == SessionMode::Share && sharePrincipal_) return vh::rbac::Actor::share(sharePrincipal_);
    throw std::runtime_error("Unauthorized");
}

void Session::logFail(std::string_view where, const beast::error_code& ec) {
    if (!ec) return;
    log::Registry::ws()->debug("[ws::Session] {}: {}", where, ec.message());
}

void Session::accept(tcp::socket&& socket) {
    uploadHandler_ = std::make_shared<handler::fs::Upload>(shared_from_this());

    ws_ = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));
    strand_ = asio::make_strand(ws_->get_executor());

    auto req = std::make_shared<RequestType>();
    auto self = shared_from_this();

    beast_http::async_read(
        ws_->next_layer(),
        tmpBuffer_,
        *req,
        asio::bind_executor(
            strand_,
            [self, req](const beast::error_code& ec, std::size_t bytesRead) {
                self->onHeadersRead(req, ec, bytesRead);
            }));
}

void Session::onHeadersRead(const std::shared_ptr<RequestType>& req, const beast::error_code& ec, std::size_t) {
    if (ec) return logFail("Error reading HTTP headers", ec);

    hydrateFromRequest(*req);

    auto self = shared_from_this();
    ws_->async_accept(
        *req,
        asio::bind_executor(
            strand_,
            [self](const beast::error_code& ec2) {
                self->onHandshakeAccepted(ec2);
            }));
}

void Session::hydrateFromRequest(const RequestType& req) {
    log::Registry::ws()->debug("[ws::Session] Handshake request received from IP: {}", getIPAddress());

    handshakeRequest_ = req;
    ipAddress = getIPAddress();
    userAgent = getUserAgent();
    shareHandshake_ = isShareHandshakeTarget(std::string_view{req.target().data(), req.target().size()});

    log::Registry::ws()->debug(
        "[ws::Session] Attempting to hydrate {} session from request. IP: {}, User-Agent: {}",
        shareHandshake_ ? "share" : "human",
        ipAddress,
        userAgent
    );

    if (shareHandshake_) {
        auth::model::RefreshToken::addShareToSession(shared_from_this(), extractCookie(req, "share_refresh"));
        runtime::Deps::get().sessionManager->tryRehydrateShareRefresh(shared_from_this());
        log::Registry::ws()->debug("[ws::Session] Share refresh cookie {} present", tokens->shareRefreshToken && !tokens->shareRefreshToken->rawToken.empty() ? "is" : "is not");
        installHandshakeDecorator();
        return;
    }

    auth::model::RefreshToken::addToSession(shared_from_this(), extractCookie(req, "refresh"));
    log::Registry::ws()->debug("[ws::Session] Human refresh cookie {} present", tokens->refreshToken && !tokens->refreshToken->rawToken.empty() ? "is" : "is not");
    runtime::Deps::get().sessionManager->tryRehydrate(shared_from_this());

    if (user) log::Registry::ws()->debug("[ws::Session] Session hydrated with user: {} (ID: {})", user->name, user->id);
    else {
        log::Registry::ws()->debug("[ws::Session] No user associated with session after hydration");
        if (!tokens->refreshToken->isValid()) exit(1); // this should never happen, but if it does, it's safest to just kill the session immediately
        installHandshakeDecorator();
    }
}

void Session::installHandshakeDecorator() const {
    const auto token = shareHandshake_
                           ? (tokens ? tokens->shareRefreshToken : nullptr)
                           : (tokens ? tokens->refreshToken : nullptr);

    if (!token) {
        log::Registry::ws()->debug("[ws::Session] No refresh token available, skipping handshake decorator installation");
        return;
    }

    const auto t = token->rawToken;
    const auto cookieName = shareHandshake_ ? std::string{"share_refresh"} : std::string{"refresh"};
    ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_->set_option(websocket::stream_base::decorator(
        [t, cookieName](websocket::response_type& res) {
            res.set(beast_http::field::server, "Vaulthalla");

            const bool isDev = config::Registry::get().dev.enabled;
            const std::string sameSite = isDev ? "Lax" : "Lax"; // keep Lax unless *need* Strict

            std::string cookie = cookieName + "=" + t +
                "; Path=/" +
                "; HttpOnly" +
                "; SameSite=" + sameSite +
                "; Max-Age=604800";

            cookie += "; Secure";

            // IMPORTANT: use insert to avoid clobbering other Set-Cookie headers
            res.insert(beast_http::field::set_cookie, cookie);
        }
    ));
}

void Session::onHandshakeAccepted(const beast::error_code& ec) {
    if (ec) return logFail("Handshake error", ec);

    log::Registry::ws()->debug("[ws::Session] Handshake accepted from IP: {}", getIPAddress());
    startReadLoop();
}

void Session::startReadLoop() {
    doRead();
}

void Session::close() {
    if (closing_.exchange(true)) return;

    if (uploadHandler_)
        uploadHandler_->abortActiveUpload("websocket_session_closed");

    auto ws = ws_;
    if (!ws) {
        buffer_.consume(buffer_.size());
        return;
    }

    auto self = shared_from_this();
    asio::post(strand_, [self, ws]() mutable {
        boost::system::error_code ec;
        if (ws->is_open())
            ws->close(websocket::close_code::normal, ec);

        if (ec)
            log::Registry::ws()->debug("[ws::Session] ws close error: {}", ec.message());

        self->ws_.reset();
        self->buffer_.consume(self->buffer_.size());

        log::Registry::ws()->debug("[ws::Session] Closed session for IP: {}", self->ipAddress);
    });
}

void Session::send(json message) {
    if (sendAccessToken_) {
        if (!tokens || !tokens->accessToken) {
            log::Registry::ws()->debug("[ws::Session] Attempted to send access token, but no access token is available in session");
            throw std::runtime_error("No access token available in session");
        }

        message["token"] = tokens->accessToken->rawToken;
        sendAccessToken_ = false;
    }

    auto payload = message.dump();
    auto self = shared_from_this();

    asio::post(strand_, [self, payload = std::move(payload)]() mutable {
        self->writeQueue_.push_back(std::move(payload));
        self->maybeStartWrite();
    });
}

void Session::maybeStartWrite() {
    if (writing_ || writeQueue_.empty()) return;
    writing_ = true;
    doWrite();
}

void Session::doWrite() {
    if (!ws_) {
        writing_ = false;
        writeQueue_.clear();
        return;
    }

    ws_->async_write(
        asio::buffer(writeQueue_.front()),
        asio::bind_executor(
            strand_,
            [self = shared_from_this()](const beast::error_code& ec, std::size_t bytesWritten) {
                self->onWrite(ec, bytesWritten);
            }));
}

void Session::onWrite(const beast::error_code& ec, std::size_t) {
    if (ec) {
        logFail("Write error", ec);
        writing_ = false;
        return;
    }

    writeQueue_.pop_front();
    if (writeQueue_.empty()) {
        writing_ = false;
        return;
    }

    doWrite();
}

void Session::doRead() {
    if (!ws_) return;

    ws_->async_read(
        buffer_,
        asio::bind_executor(
            strand_,
            [self = shared_from_this()](const beast::error_code& ec, const std::size_t bytesRead) {
                self->onRead(ec, bytesRead);
            }));
}

void Session::sendParseError(const std::string_view msg) {
    send({
        {"command", "error"},
        {"status", "parse_error"},
        {"message", std::string(msg)}
    });
}

void Session::sendInternalError() {
    send({
        {"command", "error"},
        {"status", "internal_error"},
        {"message", "An internal error occurred while processing your request."}
    });
}

void Session::onRead(const beast::error_code& ec, std::size_t) {
    if (ec == websocket::error::closed) {
        log::Registry::ws()->debug("[ws::Session] WebSocket closed gracefully by peer");
        return close();
    }
    if (ec == asio::error::eof) {
        log::Registry::ws()->debug("[ws::Session] WebSocket peer vanished (EOF)");
        return close();
    }
    if (ec) {
        logFail("Read error", ec);
        return close();
    }

    if (ws_->got_binary()) {
        try {
            uploadHandler_->handleBinaryFrame(buffer_);
        } catch (const std::exception& ex) {
            log::Registry::ws()->debug("[ws::Session] Error processing binary upload frame: {}", ex.what());
            sendInternalError();
        } catch (...) {
            log::Registry::ws()->debug("[ws::Session] Unknown error while processing binary upload frame");
            sendInternalError();
        }
    } else {
        try {
            const auto text = beast::buffers_to_string(buffer_.data());
            router_->routeMessage(json::parse(text), shared_from_this());
        } catch (const std::exception& ex) {
            log::Registry::ws()->debug("[ws::Session] Error parsing message: {}", ex.what());
            sendParseError(std::string("Failed to parse message: ") + ex.what());
            // NOTE: we still continue reading (keeps session alive after a bad frame)
        } catch (...) {
            log::Registry::ws()->debug("[ws::Session] Unknown error while processing message");
            sendInternalError();
        }
    }

    buffer_.consume(buffer_.size());
    doRead();
}

std::string Session::generateUUIDv4() {
    static boost::uuids::random_generator generator;
    const boost::uuids::uuid uuid = generator();
    return boost::uuids::to_string(uuid);
}

}
