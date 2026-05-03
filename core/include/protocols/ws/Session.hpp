#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "rbac/Actor.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>

struct UploadContext {
    std::string path;
    uint64_t expectedSize = 0;
    uint64_t bytesReceived = 0;
    std::ofstream file;
};

using RequestType = boost::beast::http::request<boost::beast::http::string_body>;

namespace vh::identities { struct User; }
namespace vh::auth::model { struct TokenPair; }
namespace vh::share { struct Principal; }

namespace vh::protocols::ws {

class Router;

namespace handler::fs { class Upload; }

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace asio      = boost::asio;
using tcp           = asio::ip::tcp;
using json          = nlohmann::json;

enum class SessionMode { Unauthenticated, Human, SharePending, Share };

class Session : public std::enable_shared_from_this<Session> {
public:
    const std::string uuid{generateUUIDv4()};
    std::shared_ptr<identities::User> user{nullptr};
    std::shared_ptr<auth::model::TokenPair> tokens;
    std::string userAgent, ipAddress;
    const std::chrono::system_clock::time_point connectionOpenedAt = std::chrono::system_clock::now();

    explicit Session(const std::shared_ptr<Router>& router);
    ~Session();

    void accept(tcp::socket&& socket);
    void send(json message);
    void close();

    void setAuthenticatedUser(const std::shared_ptr<identities::User>& u);
    void setPendingShareSession(std::string sessionId, std::string sessionToken);
    void setSharePrincipal(std::shared_ptr<vh::share::Principal> principal, std::string sessionToken);
    void clearShareSession();
    void setHandshakeRequest(const RequestType& req);

    void sendAccessTokenOnNextResponse() { sendAccessToken_ = true; }

    std::shared_ptr<handler::fs::Upload> getUploadHandler();
    [[nodiscard]] SessionMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool isSharePending() const noexcept { return mode_ == SessionMode::SharePending; }
    [[nodiscard]] bool isShareMode() const noexcept { return mode_ == SessionMode::Share; }
    [[nodiscard]] bool isShareSession() const noexcept { return isSharePending() || isShareMode(); }
    [[nodiscard]] std::shared_ptr<vh::share::Principal> sharePrincipal() const noexcept { return sharePrincipal_; }
    [[nodiscard]] vh::rbac::Actor rbacActor() const;
    [[nodiscard]] const std::string& shareSessionId() const noexcept { return shareSessionId_; }
    [[nodiscard]] const std::string& shareSessionToken() const noexcept { return shareSessionToken_; }

    static std::string generateUUIDv4();

private:
    // ---- lifecycle / io
    void startReadLoop();
    void doRead();
    void onRead(const beast::error_code& ec, std::size_t bytesRead);

    void maybeStartWrite();
    void doWrite();
    void onWrite(const beast::error_code& ec, std::size_t bytesWritten);

    // ---- handshake helpers
    void onHeadersRead(const std::shared_ptr<RequestType>& req, const beast::error_code& ec, std::size_t bytesRead);
    void onHandshakeAccepted(const beast::error_code& ec);
    void hydrateFromRequest(const RequestType& req);
    void installHandshakeDecorator() const;

    // ---- misc helpers
    static void logFail(std::string_view where, const beast::error_code& ec);
    void sendParseError(std::string_view msg);
    void sendInternalError();
    [[nodiscard]] std::string getUserAgent() const;
    [[nodiscard]] std::string getIPAddress() const;

    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    asio::any_io_executor strand_;

    beast::flat_buffer buffer_{8192};
    beast::flat_buffer tmpBuffer_{4096}; // used during HTTP header read/handshake
    RequestType handshakeRequest_;

    std::shared_ptr<handler::fs::Upload> uploadHandler_{nullptr};
    std::shared_ptr<Router> router_;
    SessionMode mode_{SessionMode::Unauthenticated};
    std::shared_ptr<vh::share::Principal> sharePrincipal_{nullptr};
    std::string shareSessionId_;
    std::string shareSessionToken_;
    bool shareHandshake_{false};

    std::atomic_bool closing_{false};

    bool writing_ = false;                 // only touched on strand
    std::deque<std::string> writeQueue_;   // only touched on strand

    bool sendAccessToken_{false};
};

}
