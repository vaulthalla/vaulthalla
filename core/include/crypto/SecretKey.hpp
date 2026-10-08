#pragma once

#include "crypto/util/encrypt.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace vh::crypto {

// A 256-bit symmetric key that is wiped when the last holder releases it. Immutable once built, so a
// std::shared_ptr<const SecretKey> snapshot can be read from any thread while rotation swaps in another one.
class SecretKey {
public:
    explicit SecretKey(std::span<const uint8_t> bytes);
    ~SecretKey();

    SecretKey(const SecretKey&) = delete;
    SecretKey& operator=(const SecretKey&) = delete;

    [[nodiscard]] std::span<const uint8_t, util::AES_KEY_SIZE> bytes() const { return bytes_; }

    [[nodiscard]] static std::shared_ptr<const SecretKey> random();

private:
    std::array<uint8_t, util::AES_KEY_SIZE> bytes_{};
};

using SecretKeyPtr = std::shared_ptr<const SecretKey>;

}
