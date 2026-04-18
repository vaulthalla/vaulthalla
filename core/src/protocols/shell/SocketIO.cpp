#include "protocols/shell/SocketIO.hpp"

#include <nlohmann/json.hpp>
#include <unistd.h>
#include <netinet/in.h>
#include <string>
#include <stdexcept>
#include <cstring>

using namespace vh::protocols::shell;

SocketIO::SocketIO(const int fd) : fd_(fd) {}

void SocketIO::send_json(const int fd, const nlohmann::json& j) {
    const std::string s = j.dump();
    const uint32_t len = htonl(s.size());
    ::write(fd, &len, 4);
    ::write(fd, s.data(), s.size());
}

nlohmann::json SocketIO::recv_json(const int fd) {
    uint32_t len_be = 0;
    if (::read(fd, &len_be, 4) != 4) throw std::runtime_error("EOF reading length");
    const uint32_t len = ntohl(len_be);
    std::string body(len, '\0');
    if (::read(fd, body.data(), len) != static_cast<ssize_t>(len))
        throw std::runtime_error("EOF reading body");
    return nlohmann::json::parse(body);
}

std::string SocketIO::next_id() {
    return "p" + std::to_string(++id_counter_);
}

void SocketIO::print(const std::string_view msg) {
    send_json(fd_, {{"type", "output"}, {"text", std::string{msg}}});
}

bool SocketIO::confirm(const std::string_view promptIn, const bool def_no) {
    auto id = next_id();
    send_json(fd_, {
        {"type", "prompt"},
        {"style", "confirm"},
        {"id", id},
        {"text", std::string{promptIn}},
        {"default", def_no ? "no" : "yes"}
    });

    while (true) {
        auto j = recv_json(fd_);
        if (j["type"] == "input" && j["id"] == id) {
            std::string v = j.value("value", "");
            std::ranges::transform(v.begin(), v.end(), v.begin(), ::tolower);
            if (v == "y" || v == "yes") return true;
            if (v == "n" || v == "no") return false;
            if (v.empty()) return !def_no;
        }
    }
}

std::string SocketIO::prompt(const std::string_view promptIn, const std::string_view def) {
    auto id = next_id();
    send_json(fd_, {
        {"type", "prompt"},
        {"style", "input"},
        {"id", id},
        {"text", std::string{promptIn}},
        {"default", std::string{def}}
    });

    while (true) {
        auto j = recv_json(fd_);
        if (j["type"] == "input" && j["id"] == id) {
            return j.value("value", std::string{def});
        }
    }
}

bool SocketIO::confirm(const std::string_view promptIn) { return confirm(promptIn, true); }
std::string SocketIO::prompt(const std::string_view promptIn) { return prompt(promptIn, ""); }
