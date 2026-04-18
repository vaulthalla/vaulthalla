#include "protocols/ws/Router.hpp"

#include "auth/session/Manager.hpp"
#include "log/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "protocols/ws/core/handler_templates.hpp"
#include "runtime/Deps.hpp"

using namespace vh::protocols::ws;
using namespace vh::protocols::ws::core;
using namespace vh::protocols::ws::model;

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

void Router::routeMessage(json&& msg, const SessionPtr& session) {
    try {
        if (!session) {
            log::Registry::ws()->error("[Router] Cannot route message: session is null");
            return;
        }

        log::Registry::ws()->debug("[Router] Routing message: {}", msg.dump());

        auto command = msg.at("command").get<std::string>();
        const std::string accessToken = msg.value("token", "");

        if (!command.starts_with("auth") && !runtime::Deps::get().sessionManager->validate(session, accessToken)) {
            log::Registry::ws()->warn("[Router] Unauthorized access attempt for command: {}", command);
            Response::UNAUTHORIZED(std::move(command), std::move(msg))(session);
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
