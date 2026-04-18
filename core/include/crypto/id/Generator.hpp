#pragma once

#include "crypto/util/uuid.hpp"

#include <sodium.h>
#include <string>

namespace vh::crypto::id {

struct IdOptions {
    // Namespace to derive a *stable*, short, unique prefix per vault (or per anything).
    // Feed this anything stable: vault UUID, DB row id, S3 bucket, etc.
    std::string_view namespace_token;

    // Number of characters to take from the derived per-namespace prefix.
    // 6 chars ~ 30 bits worth of space => negligible collision chance across namespaces.
    size_t prefix_chars = 6;

    // How many random bytes per ID (not counting prefix). 16 bytes => 128-bit => 26 chars.
    // crank this down if you want shorter IDs, at collision risk.
    size_t random_bytes = 16;

    // Separator between the namespace prefix and the random body. "_" and "-" are safe.
    char separator = '_';

    // Output case for encoded chars.
    util::Case out_case = util::Case::Upper;
};

class Generator {
public:
    explicit Generator(const IdOptions& opt)
        : options_(opt),
          ns_prefix_(derive_namespace_prefix(opt.namespace_token, opt.prefix_chars, opt.out_case)) {
        util::ensure_sodium_init();
        if (options_.random_bytes == 0) {
            throw std::invalid_argument("random_bytes must be > 0");
        }
        // Sanity: forbid separators that are likely to be annoying
        if (options_.separator == ' ' || options_.separator == '\0' || options_.separator == '\n')
            throw std::invalid_argument("bad separator");
    }

    // Generate a single ID: "<ns_prefix><sep><body>"
    // body is Crockford Base32 encoding of `random_bytes` of secure randomness.
    [[nodiscard]] std::string generate() const {
        std::string id;
        const auto body = random_body_();

        if (ns_prefix_.empty()) id = body;
        else {
            id.reserve(ns_prefix_.size() + 1 + body.size());
            id.append(ns_prefix_);
            id.push_back(options_.separator);
            id.append(body);
        }

        return id;
    }

    // Generate N IDs.
    [[nodiscard]] std::vector<std::string> generate_batch(size_t n) const {
        std::vector<std::string> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) out.push_back(generate());
        return out;
    }

    // Accessors
    [[nodiscard]] std::string_view namespace_prefix() const { return ns_prefix_; }
    [[nodiscard]] const IdOptions& options() const { return options_; }

private:
    [[nodiscard]] std::string random_body_() const {
        std::vector<uint8_t> buf(options_.random_bytes);
        randombytes_buf(buf.data(), buf.size());
        // If RFC4122-ish semantics are desired they can be embedded here, set version/variant bits
        // on the first few bytes *before* encoding. With raw Base32 bodies this is optional.
        return b32_crockford_encode(buf.data(), buf.size(), options_.out_case);
    }

    IdOptions options_;
    std::string ns_prefix_;
};

}
