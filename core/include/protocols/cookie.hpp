#pragma once

#include <string>
#include <boost/beast/http.hpp>

namespace vh::protocols {

static void trim(std::string_view& s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
}

inline std::string extractCookie(const boost::beast::http::request<boost::beast::http::string_body>& req,
                          const std::string& key) {
    const auto it = req.find(boost::beast::http::field::cookie);
    if (it == req.end()) return "";

    std::string_view cookies = it->value(); // beast::string_view compatible
    while (!cookies.empty()) {
        // Split next "name=value" chunk by ';'
        auto semi = cookies.find(';');
        std::string_view part = cookies.substr(0, semi);
        if (semi == std::string_view::npos) cookies = {};
        else cookies.remove_prefix(semi + 1);

        trim(part);
        if (part.empty()) continue;

        // Split name and value by '='
        auto eq = part.find('=');
        if (eq == std::string_view::npos) continue;

        std::string_view name = part.substr(0, eq);
        std::string_view value = part.substr(eq + 1);
        trim(name);
        trim(value);

        if (name == key) {
            // Strip optional quotes
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);

            return std::string(value);
        }
    }
    return "";
}

}
