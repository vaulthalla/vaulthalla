#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

namespace vh::rbac::permission {

template <typename EnumT, typename MaskT>
struct Set {
    using Enum = EnumT;
    using Mask = MaskT;

    static_assert(std::is_integral_v<Mask>, "Set<EnumT, MaskT>: MaskT must be an integral type");
    static_assert(std::is_unsigned_v<Mask>, "Set<EnumT, MaskT>: MaskT must be an unsigned integral type");

    Mask permissions{0};

    virtual ~Set() = default;
    Set() = default;
    explicit Set(const Mask mask) : permissions(mask) {}

    [[nodiscard]] static constexpr std::size_t bitWidth() {
        return sizeof(Mask) * 8u;
    }

    [[nodiscard]] constexpr bool has(const Enum perm) const {
        return (permissions & static_cast<Mask>(perm)) != 0;
    }

    constexpr void grant(const Enum perm) {
        permissions |= static_cast<Mask>(perm);
    }

    constexpr void revoke(const Enum perm) {
        permissions &= static_cast<Mask>(~static_cast<Mask>(perm));
    }

    [[nodiscard]] constexpr Mask operator()() const { return permissions; }
    [[nodiscard]] constexpr Mask raw() const { return permissions; }

    [[nodiscard]] std::string toBitString() const {
        std::string bits;
        bits.reserve(bitWidth());

        for (std::size_t i = 0; i < bitWidth(); ++i) {
            const auto shift = static_cast<unsigned>(bitWidth() - 1u - i);
            bits.push_back(((permissions >> shift) & static_cast<Mask>(1)) ? '1' : '0');
        }

        return bits;
    }

    constexpr void setRaw(const Mask mask) { permissions = mask; }
    constexpr void clear() { permissions = 0; }

    [[nodiscard]] virtual std::string toString(uint8_t indent) const = 0;

protected:
    [[nodiscard]] static std::string bool_to_string(const bool value) {
        return value ? "true" : "false";
    }
};

}
