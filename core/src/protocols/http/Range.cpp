#include "protocols/http/Range.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace vh::protocols::http::range {

namespace {

[[nodiscard]] std::string_view trim(std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    return v;
}

[[nodiscard]] std::optional<uint64_t> number(const std::string_view v) {
    if (v.empty() || !std::ranges::all_of(v, [](const unsigned char c) { return std::isdigit(c); })) return std::nullopt;
    uint64_t out = 0;
    const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    if (ec != std::errc{} || ptr != v.data() + v.size()) return std::nullopt;  // includes overflow
    return out;
}

[[nodiscard]] std::optional<Spec> parseOne(std::string_view item) {
    item = trim(item);
    const auto dash = item.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    const auto firstRaw = trim(item.substr(0, dash));
    const auto lastRaw = trim(item.substr(dash + 1));
    Spec spec;
    if (firstRaw.empty()) {
        const auto suffix = number(lastRaw);
        if (!suffix) return std::nullopt;
        spec.last = *suffix;
        return spec;
    }
    spec.first = number(firstRaw);
    if (!spec.first) return std::nullopt;
    if (!lastRaw.empty()) {
        spec.last = number(lastRaw);
        if (!spec.last || *spec.last < *spec.first) return std::nullopt;
    }
    return spec;
}

}

Parsed parse(std::string_view header) {
    Parsed out;
    header = trim(header);
    constexpr std::string_view unit = "bytes";
    if (header.size() <= unit.size() || header[unit.size()] != '=') {
        out.malformed = true;
        return out;
    }
    for (std::size_t i = 0; i < unit.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(header[i])) != unit[i]) {
            out.malformed = true;
            return out;
        }

    auto rest = header.substr(unit.size() + 1);
    std::size_t count = 0;
    std::optional<Spec> first;
    while (true) {
        const auto comma = rest.find(',');
        const auto item = rest.substr(0, comma);
        if (!trim(item).empty()) {  // RFC 9110 allows empty list elements
            const auto spec = parseOne(item);
            if (!spec) {
                out.malformed = true;
                return out;
            }
            if (++count == 1) first = spec;
        }
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }

    if (count == 0) out.malformed = true;
    else if (count > 1) out.multi = true;
    else out.spec = first;
    return out;
}

std::optional<Resolved> resolve(const Spec& spec, const uint64_t size) {
    if (size == 0) return std::nullopt;
    if (!spec.first) {
        if (!spec.last || *spec.last == 0) return std::nullopt;
        const auto n = std::min(*spec.last, size);
        return Resolved{size - n, size - 1};
    }
    if (*spec.first >= size) return std::nullopt;
    const auto last = spec.last ? std::min(*spec.last, size - 1) : size - 1;
    if (last < *spec.first) return std::nullopt;
    return Resolved{*spec.first, last};
}

std::string contentRange(const Resolved& range, const uint64_t size) {
    return "bytes " + std::to_string(range.first) + "-" + std::to_string(range.last) + "/" + std::to_string(size);
}

std::string unsatisfiedContentRange(const uint64_t size) {
    return "bytes */" + std::to_string(size);
}

}
