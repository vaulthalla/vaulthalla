#pragma once

#include <sodium.h>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <algorithm>

namespace vh::crypto::util {

// ---------- Alphabet: Crockford Base32 (no I, L, O, U) – filesystem/email safe
// 32 symbols => each char encodes 5 bits; 128-bit payload => 26 chars
static inline constexpr char kBase32Crockford[] =
    "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; // [0..31]

enum class Case { Upper, Lower };

// ---------- Small helper: sodium init (thread-safe, idempotent)
inline void ensure_sodium_init() {
    static const int init = []{
        if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
        return 1;
    }();
    (void)init;
}

// ---------- Base32 (Crockford) encode for arbitrary byte buffers
inline std::string b32_crockford_encode(const uint8_t* data, const size_t len, const Case out_case = Case::Upper) {
    if (len == 0) return {};
    // 5-bit packing
    std::string out;
    out.reserve((len * 8 + 4) / 5);

    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < len; ++i) {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            const uint8_t idx = (buffer >> bits) & 0x1F;
            out.push_back(kBase32Crockford[idx]);
        }
    }

    if (bits > 0) {
        const uint8_t idx = (buffer << (5 - bits)) & 0x1F;
        out.push_back(kBase32Crockford[idx]);
    }

    if (out_case == Case::Lower)
        std::ranges::transform(out.begin(), out.end(), out.begin(),
            [](const unsigned char c){ return static_cast<char>(std::tolower(c)); });

    return out;
}

// ---------- RFC 4122 v4 UUID (hex string)
inline std::string uuid4_hex() {
    ensure_sodium_init();
    std::array<uint8_t,16> b{};
    randombytes_buf(b.data(), b.size());
    b[6] = (b[6] & 0x0F) | 0x40; // version 4
    b[8] = (b[8] & 0x3F) | 0x80; // variant

    char s[37];
    std::snprintf(s, sizeof s,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return {s};
}


struct PrefixKey {
    uint8_t version = 1;                         // for rotation / migrations
    std::array<unsigned char, 32> key{};         // fill from secure config/TPM
    bool enabled = false;                        // default: unkeyed behavior
};

inline std::string derive_namespace_prefix(const std::string_view namespace_token,
                                           const size_t prefix_chars,
                                           const Case out_case,
                                           const PrefixKey& pfx_key = {}) {
    if (namespace_token.empty() || prefix_chars == 0) return {};

    // Purpose string to bind the function use
    static constexpr char kCtx[] = "vh/ns-prefix/v1";

    std::array<uint8_t, 16> digest{}; // 128-bit digest -> enough for short prefixes

    crypto_generichash_state st;
    if (crypto_generichash_init(&st,
            pfx_key.enabled ? pfx_key.key.data() : nullptr,
            pfx_key.enabled ? pfx_key.key.size() : 0,
            digest.size()) != 0) {
        throw std::runtime_error("blake2 init failed");
    }

    // domain separation
    crypto_generichash_update(&st,
        reinterpret_cast<const unsigned char*>(kCtx), sizeof(kCtx)-1);

    // include key version if using a key
    if (pfx_key.enabled) crypto_generichash_update(&st, &pfx_key.version, sizeof(pfx_key.version));

    // hash the namespace token
    crypto_generichash_update(&st,
        reinterpret_cast<const unsigned char*>(namespace_token.data()),
        namespace_token.size());

    crypto_generichash_final(&st, digest.data(), digest.size());

    // Encode and trim
    std::string enc = b32_crockford_encode(digest.data(), digest.size(), out_case);

    // prepend a single Crockford char for key version to rotate safely
    if (pfx_key.enabled) {
        const char ver = kBase32Crockford[pfx_key.version & 0x1F];
        enc.insert(enc.begin(), ver);
    }

    if (enc.size() < prefix_chars) enc.append(prefix_chars - enc.size(), '0');
    enc.resize(prefix_chars);
    return enc;
}

}
