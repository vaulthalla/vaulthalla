#include "protocols/ws/model/Response.hpp"
#include "protocols/ws/Session.hpp"

#include <format>

using namespace vh::protocols::ws::model;

Response::Response(std::string&& cmd, json&& req, const Status& status, json&& data, std::optional<std::string>&& error)
    : cmd(std::move(cmd)), req(std::move(req)), data(std::move(data)), status(status), error(std::move(error)) {}

void Response::operator()(const std::shared_ptr<Session>& session) {
    if (!session) return;

    json response = {
        {"command", std::format("{}.response", cmd)},
        {"status", to_string(status)},
    };

    if (req.is_object() && req.contains("requestId")) response["requestId"] = req.at("requestId").get<std::string>();

    if (!data.empty()) response["data"] = data;
    if (error) response["error"] = error;

    session->send(response);
}

Response Response::SUCCESS(std::string&& cmd, json&& req, json&& data) {
    return {std::move(cmd), std::move(req), Status::OK, std::move(data)};
}

Response Response::ERROR(std::string&& cmd, json&& req, std::string&& error) {
    return {std::move(cmd), std::move(req), Status::ERROR, {}, error};
}

Response Response::UNAUTHORIZED(std::string&& cmd, json&& req) {
    return {std::move(cmd), std::move(req), Status::UNAUTHORIZED, {},
        "You must be authenticated to perform this action."};
}

Response Response::INTERNAL_ERROR(json&& req, std::string&& error) {
    return {"Unknown", std::move(req), Status::INTERNAL_ERROR, {},
        "An internal error occurred while processing your request: " + error};
}

std::string vh::protocols::ws::model::to_string(const Status& status) {
    switch (status) {
        case Status::OK: return "OK";
        case Status::ERROR: return "ERROR";
        case Status::UNAUTHORIZED: return "UNAUTHORIZED";
        default: return "UNKNOWN";
    }
}
