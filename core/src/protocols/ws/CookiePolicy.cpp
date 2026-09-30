#include "protocols/ws/CookiePolicy.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace vh::protocols::ws::cookie_policy {

namespace {
std::string lowerTrim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\"");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\"");
    std::string out(value.substr(first, last - first + 1));
    std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// First hop only: the proxy closest to the browser is the one listed first.
std::string_view firstListElement(std::string_view value) {
    const auto comma = value.find(',');
    return comma == std::string_view::npos ? value : value.substr(0, comma);
}
}

bool isLoopbackAddress(const std::string_view address) {
    return address == "::1" || address.starts_with("127.") || address.starts_with("::ffff:127.");
}

bool isExternallyHttps(const std::string_view peerAddress, const std::string_view xForwardedProto,
                       const std::string_view forwarded) {
    if (!isLoopbackAddress(peerAddress)) return false;

    if (!xForwardedProto.empty()) return lowerTrim(firstListElement(xForwardedProto)) == "https";

    // RFC 7239: Forwarded: for=...;proto=https;host=...
    const auto element = firstListElement(forwarded);
    std::size_t pos = 0;
    while (pos < element.size()) {
        const auto semi = element.find(';', pos);
        const auto pair = element.substr(pos, semi == std::string_view::npos ? std::string_view::npos : semi - pos);
        if (const auto eq = pair.find('='); eq != std::string_view::npos && lowerTrim(pair.substr(0, eq)) == "proto")
            return lowerTrim(pair.substr(eq + 1)) == "https";
        if (semi == std::string_view::npos) break;
        pos = semi + 1;
    }
    return false;
}

}
