#pragma once

#include "protocols/ws/Router.hpp"
#include "protocols/ws/model/Response.hpp"

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <utility>

using json = nlohmann::json;

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::core {

template <class Fn>
Router::Handler makeWsHandler(std::string cmd, Fn&& fn) {
    return [cmd = std::move(cmd), fn = std::forward<Fn>(fn)]
           (json&& msg, const std::shared_ptr<Session>& session) mutable {
        try {
            json data = std::invoke(fn, msg, session);
            model::Response::SUCCESS(std::string(cmd), std::move(msg), std::move(data))(session);
        } catch (const std::exception& e) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string(e.what()))(session);
        } catch (...) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string("Unknown error"))(session);
        }
    };
}

template <class Fn>
Router::Handler makePayloadHandler(std::string cmd, Fn&& fn) {
    return [cmd = std::move(cmd), fn = std::forward<Fn>(fn)]
           (json&& msg, const std::shared_ptr<Session>& session) mutable {
        try {
            const json& payload = msg.at("payload");
            json data = std::invoke(fn, payload, session);
            model::Response::SUCCESS(std::string(cmd), std::move(msg), std::move(data))(session);
        } catch (const std::exception& e) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string(e.what()))(session);
        } catch (...) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string("Unknown error"))(session);
        }
    };
}

template <class Fn>
Router::Handler makeHandlerWithToken(std::string cmd, Fn&& fn) {
    return [cmd = std::move(cmd), fn = std::forward<Fn>(fn)]
           (json&& msg, const std::shared_ptr<Session>& session) mutable {
        try {
            const auto& token = msg.at("token").get_ref<const std::string&>();
            json data = std::invoke(fn, token, session);
            model::Response::SUCCESS(std::string(cmd), std::move(msg), std::move(data))(session);
        } catch (const std::exception& e) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string(e.what()))(session);
        } catch (...) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string("Unknown error"))(session);
        }
    };
}

template <class Fn>
Router::Handler makeSessionOnlyHandler(std::string cmd, Fn&& fn) {
    return [cmd = std::move(cmd), fn = std::forward<Fn>(fn)]
           (json&& msg, const std::shared_ptr<Session>& session) mutable {
        try {
            json data = std::invoke(fn, session);
            model::Response::SUCCESS(std::string(cmd), std::move(msg), std::move(data))(session);
        } catch (const std::exception& e) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string(e.what()))(session);
        } catch (...) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string("Unknown error"))(session);
        }
    };
}

template <class Fn>
Router::Handler makeEmptyHandler(std::string cmd, Fn&& fn) {
    return [cmd = std::move(cmd), fn = std::forward<Fn>(fn)]
           (json&& msg, const std::shared_ptr<Session>& session) mutable {
        try {
            json data = std::invoke(fn);
            model::Response::SUCCESS(std::string(cmd), std::move(msg), std::move(data))(session);
        } catch (const std::exception& e) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string(e.what()))(session);
        } catch (...) {
            model::Response::ERROR(std::string(cmd), std::move(msg), std::string("Unknown error"))(session);
        }
    };
}

// ---------------------------
// helpers to bind member functions cleanly
// ---------------------------

template <class Obj>
auto bindWsMember(Obj* obj, json (Obj::*mf)(const json&, const std::shared_ptr<Session>&)) {
    return [obj, mf](const json& msg, const std::shared_ptr<Session>& session) {
        return (obj->*mf)(msg, session);
    };
}

template <class Obj>
auto bindWsMember(const Obj* obj, json (Obj::*mf)(const json&, const std::shared_ptr<Session>&) const) {
    return [obj, mf](const json& msg, const std::shared_ptr<Session>& session) {
        return (obj->*mf)(msg, session);
    };
}

template <class Obj>
auto bindPayloadMember(Obj* obj, json (Obj::*mf)(const json&, const std::shared_ptr<Session>&)) {
    return [obj, mf](const json& payload, const std::shared_ptr<Session>& session) {
        return (obj->*mf)(payload, session);
    };
}

template <class Obj>
auto bindPayloadMember(const Obj* obj, json (Obj::*mf)(const json&, const std::shared_ptr<Session>&) const) {
    return [obj, mf](const json& payload, const std::shared_ptr<Session>& session) {
        return (obj->*mf)(payload, session);
    };
}

template <class Obj>
auto bindTokenMember(Obj* obj, json (Obj::*mf)(const std::string&, const std::shared_ptr<Session>&)) {
    return [obj, mf](const std::string& token, const std::shared_ptr<Session>& session) {
        return (obj->*mf)(token, session);
    };
}

template <class Obj>
auto bindTokenMember(const Obj* obj, json (Obj::*mf)(const std::string&, const std::shared_ptr<Session>&) const) {
    return [obj, mf](const std::string& token, const std::shared_ptr<Session>& session) {
        return (obj->*mf)(token, session);
    };
}

template <class Obj>
auto bindSessionOnlyMember(Obj* obj, json (Obj::*mf)(const std::shared_ptr<Session>&)) {
    return [obj, mf](const std::shared_ptr<Session>& session) {
        return (obj->*mf)(session);
    };
}

template <class Obj>
auto bindSessionOnlyMember(const Obj* obj, json (Obj::*mf)(const std::shared_ptr<Session>&) const) {
    return [obj, mf](const std::shared_ptr<Session>& session) {
        return (obj->*mf)(session);
    };
}

template <class Obj>
auto bindEmptyMember(Obj* obj, json (Obj::*mf)()) {
    return [obj, mf]() {
        return (obj->*mf)();
    };
}

template <class Obj>
auto bindEmptyMember(const Obj* obj, json (Obj::*mf)() const) {
    return [obj, mf]() {
        return (obj->*mf)();
    };
}

}
