#pragma once

#include <nlohmann/json.hpp>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::model {

using json = nlohmann::json;

enum class Status { OK, ERROR, UNAUTHORIZED, INTERNAL_ERROR };

struct Response {
    std::string cmd;
    json req, data{};
    Status status = Status::OK;
    std::optional<std::string> error{};

    Response(std::string&& cmd, json&& req, const Status& status, json&& data = json{}, std::optional<std::string>&& error = std::nullopt);

    void operator()(const std::shared_ptr<Session>& session);

    static Response SUCCESS(std::string&& cmd, json&& req, json&& data);

    static Response ERROR(std::string&& cmd, json&& req, std::string&& error);

    static Response UNAUTHORIZED(std::string&& cmd, json&& req);

    static Response INTERNAL_ERROR(json&& req, std::string&& error = "Unknown");
};

std::string to_string(const Status& status);

}
