#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// RFC 9110 single byte-range handling shared by the HTTP lanes and the S3 gateway.
namespace vh::protocols::http::range {

// One byte-range-spec as written: "a-b" (first+last), "a-" (first only), "-n" (suffix: last holds n).
struct Spec {
    std::optional<uint64_t> first;
    std::optional<uint64_t> last;
};

struct Parsed {
    std::optional<Spec> spec;   // set for exactly one well-formed range
    bool multi{false};          // well-formed but more than one range ("a-b,c-d")
    bool malformed{false};      // syntactically invalid (RFC 9110: ignore the header)
};

// Parses a Range header value ("bytes=…", unit case-insensitive, optional whitespace). Never throws.
[[nodiscard]] Parsed parse(std::string_view header);

struct Resolved {
    uint64_t first{};
    uint64_t last{};   // inclusive
    [[nodiscard]] uint64_t length() const { return last - first + 1; }
};

// Resolves a spec against a representation of `size` bytes. nullopt ⇒ unsatisfiable (416).
[[nodiscard]] std::optional<Resolved> resolve(const Spec& spec, uint64_t size);

// "bytes first-last/size"
[[nodiscard]] std::string contentRange(const Resolved& range, uint64_t size);
// "bytes */size" (416)
[[nodiscard]] std::string unsatisfiedContentRange(uint64_t size);

}
