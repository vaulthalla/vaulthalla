#pragma once

#include "rbac/permission/template/Set.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace vh::rbac::permission {

template <typename MaskT>
struct Module {
    using Mask = MaskT;

    static_assert(std::is_integral_v<Mask>, "Module<MaskT>: MaskT must be an integral type");
    static_assert(std::is_unsigned_v<Mask>, "Module<MaskT>: MaskT must be an unsigned integral type");

    virtual ~Module() = default;
    Module() = default;

    [[nodiscard]] static constexpr std::size_t bitWidth() {
        return sizeof(Mask) * 8u;
    }

    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual Mask toMask() const = 0;
    virtual void fromMask(Mask mask) = 0;

    [[nodiscard]] std::string toBitString() const {
        return maskToBitString(toMask());
    }

    [[nodiscard]] static std::string maskToBitString(const Mask mask) {
        std::string bits;
        bits.reserve(bitWidth());

        for (std::size_t i = 0; i < bitWidth(); ++i) {
            const auto shift = static_cast<unsigned>(bitWidth() - 1u - i);
            bits.push_back(((mask >> shift) & static_cast<Mask>(1)) ? '1' : '0');
        }

        return bits;
    }

    [[nodiscard]] virtual std::string toString(uint8_t indent) const = 0;

protected:
    template <typename EnumT, typename SetMaskT>
    static void append(Mask& dstMask, uint8_t& offset, const Set<EnumT, SetMaskT>& set, const char* context) {
        constexpr auto setBits = Set<EnumT, SetMaskT>::bitWidth();

        if (offset + setBits > bitWidth())
            throw std::runtime_error(std::string(context) + ": bit width exceeds destination mask");

        dstMask |= static_cast<Mask>(set.raw()) << offset;
        offset += static_cast<uint8_t>(setBits);
    }

    template <typename EnumT, typename SetMaskT>
    static void extract(const Mask srcMask, uint8_t& offset, Set<EnumT, SetMaskT>& set, const char* context) {
        constexpr auto setBits = Set<EnumT, SetMaskT>::bitWidth();

        if (offset + setBits > bitWidth())
            throw std::runtime_error(std::string(context) + ": bit width exceeds source mask");

        constexpr Mask fieldMask =
            setBits >= bitWidth()
                ? static_cast<Mask>(~static_cast<Mask>(0))
                : static_cast<Mask>((static_cast<Mask>(1) << setBits) - 1);

        const auto value = static_cast<SetMaskT>((srcMask >> offset) & fieldMask);
        set.setRaw(value);

        offset += static_cast<uint8_t>(setBits);
    }

    template <typename... SetTs>
    [[nodiscard]] Mask pack(const SetTs&... sets) const {
        Mask mask = 0;
        uint8_t offset = 0;
        (append(mask, offset, sets, name()), ...);
        return mask;
    }

    template <typename... SetTs>
    void unpack(const Mask srcMask, SetTs&... sets) const {
        uint8_t offset = 0;
        (extract(srcMask, offset, sets, name()), ...);
    }

    [[nodiscard]] static std::string bool_to_string(const bool value) {
        return value ? "true" : "false";
    }
};

}
