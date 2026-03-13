#pragma once

#include "rbac/permission/template/Module.hpp"

#include <cstdint>
#include <string>
#include <type_traits>

namespace vh::rbac::permission {

template <typename MaskT, typename SetEnumT, typename SetMaskT>
struct ModuleSet : Module<MaskT> {
    using Base = Module<MaskT>;
    using Mask = typename Base::Mask;
    using Enum = SetEnumT;
    using SetMask = SetMaskT;

    static_assert(std::is_enum_v<Enum>, "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetEnumT must be an enum type");
    static_assert(std::is_integral_v<SetMask>, "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetMaskT must be an integral type");
    static_assert(std::is_unsigned_v<SetMask>, "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetMaskT must be an unsigned integral type");
    static_assert(sizeof(SetMask) <= sizeof(Mask), "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetMaskT cannot exceed MaskT width");

    SetMask permissions{0};

    ModuleSet() = default;
    explicit ModuleSet(const SetMask mask) : permissions(mask) {}

    [[nodiscard]] static constexpr std::size_t setBitWidth() {
        return sizeof(SetMask) * 8u;
    }

    [[nodiscard]] constexpr bool has(const Enum perm) const {
        return (permissions & static_cast<SetMask>(perm)) != 0;
    }

    constexpr void grant(const Enum perm) {
        permissions |= static_cast<SetMask>(perm);
    }

    constexpr void revoke(const Enum perm) {
        permissions &= static_cast<SetMask>(~static_cast<SetMask>(perm));
    }

    [[nodiscard]] constexpr SetMask operator()() const { return permissions; }
    [[nodiscard]] constexpr SetMask raw() const { return permissions; }

    constexpr void setRaw(const SetMask mask) { permissions = mask; }
    constexpr void clear() { permissions = 0; }

    [[nodiscard]] std::string toBitString() const {
        return Base::maskToBitString(static_cast<Mask>(permissions));
    }

    [[nodiscard]] Mask toMask() const override {
        return static_cast<Mask>(permissions);
    }

    void fromMask(const Mask mask) override {
        if constexpr (sizeof(SetMask) < sizeof(Mask)) {
            constexpr auto setBits = sizeof(SetMask) * 8u;
            constexpr Mask fieldMask =
                setBits >= Base::bitWidth()
                    ? static_cast<Mask>(~static_cast<Mask>(0))
                    : static_cast<Mask>((static_cast<Mask>(1) << setBits) - 1);

            permissions = static_cast<SetMask>(mask & fieldMask);
        } else {
            permissions = static_cast<SetMask>(mask);
        }
    }

protected:
    template <typename... SetTs>
    [[nodiscard]] Mask packWithOwn(const SetTs&... sets) const {
        Mask mask = 0;
        uint8_t offset = 0;

        Base::append(mask, offset, *this, this->name());
        (Base::append(mask, offset, sets, this->name()), ...);

        return mask;
    }

    template <typename... SetTs>
    void unpackWithOwn(const Mask srcMask, SetTs&... sets) {
        uint8_t offset = 0;

        Base::extract(srcMask, offset, *this, this->name());
        (Base::extract(srcMask, offset, sets, this->name()), ...);
    }

public:
    constexpr void setRawFromSetMask(const SetMask mask) {
        permissions = mask;
    }

    [[nodiscard]] constexpr SetMask rawSetMask() const {
        return permissions;
    }
};

}
