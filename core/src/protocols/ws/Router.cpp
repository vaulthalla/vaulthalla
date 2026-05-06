#include "protocols/ws/Router.hpp"

#include "auth/session/Manager.hpp"
#include "log/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/ShareRateLimit.hpp"
#include "protocols/ws/core/handler_templates.hpp"
#include "runtime/Deps.hpp"

#include <algorithm>
#include <array>

namespace {
template<size_t N>
bool containsCommand(const std::array<std::string_view, N>& commands, const std::string_view command) {
    return std::ranges::find(commands, command) != commands.end();
}

bool isAuthCommand(const std::string_view command) {
    return command.starts_with("auth");
}

vh::protocols::ws::ShareRateLimit& shareRateLimit() {
    static vh::protocols::ws::ShareRateLimit limiter;
    return limiter;
}
}

namespace vh::protocols::ws {

using namespace core;
using namespace model;

void Router::registerWs(const std::string& cmd, RawWsHandler fn) {
    handlers_[cmd] = makeWsHandler(cmd, std::move(fn));
}

void Router::registerPayload(const std::string& cmd, RawPayloadHandler fn) {
    handlers_[cmd] = makePayloadHandler(cmd, std::move(fn));
}

void Router::registerPayloadOnly(const std::string &cmd, RawPayloadHandlerOnly fn) {
    handlers_[cmd] = makePayloadOnlyHandler(cmd, std::move(fn));
}

void Router::registerHandlerWithToken(const std::string& cmd, RawHandlerWithToken fn) {
    handlers_[cmd] = makeHandlerWithToken(cmd, std::move(fn));
}

void Router::registerSessionOnlyHandler(const std::string& cmd, RawSessionOnly fn) {
    handlers_[cmd] = makeSessionOnlyHandler(cmd, std::move(fn));
}

void Router::registerEmptyHandler(const std::string& cmd, RawEmpty fn) {
    handlers_[cmd] = makeEmptyHandler(cmd, std::move(fn));
}

void Router::registerHandler(const std::string& cmd, Handler h) {
    handlers_[cmd] = std::move(h);
}

bool Router::isPublicShareCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"share.session.open"},
        std::string_view{"share.email.challenge.start"},
        std::string_view{"share.email.challenge.confirm"}
    };
    return containsCommand(commands, command);
}

bool Router::isShareFilesystemCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"share.fs.metadata"},
        std::string_view{"share.fs.list"}
    };
    return containsCommand(commands, command);
}

namespace {
bool isShareNativeFilesystemCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"fs.metadata"},
        std::string_view{"fs.list"}
    };
    return containsCommand(commands, command);
}

bool isShareNativeDownloadCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"fs.download.start"},
        std::string_view{"fs.download.chunk"},
        std::string_view{"fs.download.cancel"}
    };
    return containsCommand(commands, command);
}

bool isShareNativeUploadCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"fs.upload.start"},
        std::string_view{"fs.upload.finish"},
        std::string_view{"fs.upload.cancel"}
    };
    return containsCommand(commands, command);
}
}

bool Router::isShareDownloadCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"share.download.start"},
        std::string_view{"share.download.chunk"},
        std::string_view{"share.download.cancel"}
    };
    return containsCommand(commands, command);
}

bool Router::isSharePreviewCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"share.preview.get"}
    };
    return containsCommand(commands, command);
}

bool Router::isShareUploadCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"share.upload.start"},
        std::string_view{"share.upload.finish"},
        std::string_view{"share.upload.cancel"}
    };
    return containsCommand(commands, command);
}

bool Router::isShareModeCommand(const std::string_view command) {
    return isPublicShareCommand(command) ||
           isShareFilesystemCommand(command) ||
           isShareNativeFilesystemCommand(command) ||
           isShareNativeDownloadCommand(command) ||
           isShareNativeUploadCommand(command) ||
           isShareDownloadCommand(command) ||
           isSharePreviewCommand(command) ||
           isShareUploadCommand(command);
}

bool Router::isAuthenticatedShareManagementCommand(const std::string_view command) {
    constexpr std::array commands{
        std::string_view{"share.link.create"},
        std::string_view{"share.link.get"},
        std::string_view{"share.link.list"},
        std::string_view{"share.link.update"},
        std::string_view{"share.link.revoke"},
        std::string_view{"share.link.rotate_token"}
    };
    return containsCommand(commands, command);
}

Router::CommandAuthDecision Router::classifyCommand(const std::string_view command, const Session& session) {
    if (session.isSharePending()) {
        return isPublicShareCommand(command) ? CommandAuthDecision::Allow : CommandAuthDecision::Deny;
    }

    if (session.isShareMode()) {
        return isShareModeCommand(command) ? CommandAuthDecision::Allow : CommandAuthDecision::Deny;
    }

    if (session.user) {
        if (isPublicShareCommand(command) ||
            isShareFilesystemCommand(command) ||
            isShareDownloadCommand(command) ||
            isSharePreviewCommand(command) ||
            isShareUploadCommand(command))
            return CommandAuthDecision::Deny;
        if (isAuthCommand(command)) return CommandAuthDecision::Allow;
        return CommandAuthDecision::RequireHumanAuth;
    }

    if (isAuthCommand(command) || isPublicShareCommand(command)) return CommandAuthDecision::Allow;
    return CommandAuthDecision::Deny;
}

void Router::routeMessage(json&& msg, const SessionPtr& session) {
    try {
        if (!session) {
            log::Registry::ws()->error("[Router] Cannot route message: session is null");
            return;
        }

        log::Registry::ws()->debug("[Router] Routing message: {}", msg.dump());

        auto command = msg.at("command").get<std::string>();
        const std::string accessToken = msg.value("token", "");

        const auto decision = classifyCommand(command, *session);

        if (decision == CommandAuthDecision::Deny) {
            log::Registry::ws()->warn("[Router] Unauthorized access attempt for command: {}", command);
            Response::UNAUTHORIZED(std::move(command), std::move(msg))(session);
            return;
        }

        if (decision == CommandAuthDecision::RequireHumanAuth &&
            !runtime::Deps::get().sessionManager->validate(session, accessToken)) {
            log::Registry::ws()->warn("[Router] Unauthorized access attempt for command: {}", command);
            Response::UNAUTHORIZED(std::move(command), std::move(msg))(session);
            return;
        }

        const auto rateLimit = shareRateLimit().check(command, msg, *session);
        if (!rateLimit.allowed) {
            log::Registry::ws()->warn(
                "[Router] Share command rate limited: {} for IP {}",
                command,
                session->ipAddress.empty() ? "unknown" : session->ipAddress
            );
            Response::ERROR(std::move(command), std::move(msg), "Share command rate limit exceeded. Try again later.")(session);
            return;
        }

        if (handlers_.contains(command)) handlers_[command](std::move(msg), session);
        else {
            log::Registry::ws()->warn("[Router] Unknown command: {}", command);
            Response::ERROR(std::move(command), std::move(msg), "Unknown command")(session);
        }
    } catch (const std::exception& e) {
        log::Registry::ws()->error("[Router] Error routing message: {}", e.what());
        if (session) Response::INTERNAL_ERROR(std::move(msg), e.what())(session);
    }
}

}
