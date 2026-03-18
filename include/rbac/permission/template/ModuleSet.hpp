#pragma once

#include "rbac/permission/template/Module.hpp"
#include "Description.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace vh::rbac::permission {
    template<typename MaskT, typename SetEnumT, typename SetMaskT>
    struct ModuleSet : Module<MaskT> {
        using Base = Module<MaskT>;
        using Mask = typename Base::Mask;
        using Enum = SetEnumT;
        using SetMask = SetMaskT;

        static_assert(std::is_enum_v<Enum>, "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetEnumT must be an enum type");
        static_assert(std::is_integral_v<SetMask>,
                      "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetMaskT must be an integral type");
        static_assert(std::is_unsigned_v<SetMask>,
                      "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetMaskT must be an unsigned integral type");
        static_assert(sizeof(SetMask) <= sizeof(Mask),
                      "ModuleSet<MaskT, SetEnumT, SetMaskT>: SetMaskT cannot exceed MaskT width");

        SetMask permissions{0};

        ModuleSet() = default;

        explicit ModuleSet(const SetMask mask) : permissions(mask) {
        }

        [[nodiscard]] virtual const char *flagPrefix() const = 0;

        [[nodiscard]] static constexpr std::size_t setBitWidth() {
            return sizeof(SetMask) * 8u;
        }

        [[nodiscard]] static constexpr std::size_t bitWidth() {
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

        bool applyFlag(std::string_view flag,
                       std::string_view allowPrefix = "--allow-",
                       std::string_view denyPrefix = "--deny-")
            requires HasPermissionTraits<Enum> {
            if (flag.starts_with(allowPrefix)) {
                const auto slug = flag.substr(allowPrefix.size());
                for (const auto &entry: PermissionTraits<Enum>::entries)
                    if (entry.slug == slug) {
                        grant(entry.value);
                        return true;
                    }
                return false;
            }

            if (flag.starts_with(denyPrefix)) {
                const auto slug = flag.substr(denyPrefix.size());
                for (const auto &entry: PermissionTraits<Enum>::entries)
                    if (entry.slug == slug) {
                        revoke(entry.value);
                        return true;
                    }
                return false;
            }

            return false;
        }

        template<typename Describer, typename OutputIt>
        void exportPermissions(
            const Describer &describer,
            OutputIt out,
            const std::size_t baseOffset,
            std::string_view qualifiedPrefix,
            std::string_view descriptionPrefix = {}
        ) const
            requires HasPermissionTraits<Enum> {
            for (const auto &entry: PermissionTraits<Enum>::entries) {
                const auto raw = static_cast<SetMask>(entry.value);
                if (!hasSingleBit(raw))
                    continue;

                const auto localBit = singleBitIndex(raw);
                const auto globalBit = baseOffset + localBit;

                std::string qualifiedName;
                qualifiedName.reserve(qualifiedPrefix.size() + 1 + entry.slug.size());
                qualifiedName.append(qualifiedPrefix);
                qualifiedName.push_back('.');
                qualifiedName.append(entry.slug);

                *out++ = Permission{
                    static_cast<uint32_t>(globalBit),
                    std::move(qualifiedName),
                    buildPermissionDescription(describer, entry.description, descriptionPrefix)
                };
            }
        }

    protected:
        template<typename... SetTs>
        [[nodiscard]] Mask packWithOwn(const SetTs &... sets) const {
            Mask mask = 0;
            typename Base::Offset offset = 0;

            Base::append(mask, offset, *this, this->name());
            (Base::append(mask, offset, sets, this->name()), ...);

            return mask;
        }

        template<typename... SetTs>
        void unpackWithOwn(const Mask srcMask, SetTs &... sets) {
            typename Base::Offset offset = 0;

            Base::extract(srcMask, offset, *this, this->name());
            (Base::extract(srcMask, offset, sets, this->name()), ...);
        }

        template<typename... PermissionTs>
        [[nodiscard]] std::string joinFlagsWithOwn(const PermissionTs &... p) const {
            std::ostringstream oss;
            bool first = true;

            auto append = [&](const auto &permission) {
                const auto flags = permission.toFlagsString();
                if (flags.empty()) return;

                if (!first) oss << ' ';
                first = false;
                oss << flags;
            };

            append(*this);
            (append(p), ...);
            return oss.str();
        }

        template<typename... SetTs>
        [[nodiscard]] PackedPermissionExportT<Mask> packAndExportWithOwn(
            std::string_view ownQualifiedName,
            const MountedSetRef<SetTs> &... sets
        ) const
            requires HasPermissionTraits<Enum> {
            PackedPermissionExportT<Mask> result{};
            typename Base::Offset offset = 0;

            Base::appendAndExport(
                result.mask,
                offset,
                mount(ownQualifiedName, *this),
                this->name(),
                result.permissions
            );

            (Base::appendAndExport(result.mask, offset, sets, this->name(), result.permissions), ...);

            return result;
        }

        [[nodiscard]] static constexpr uint32_t singleBitIndex(const SetMask value) {
            uint32_t index = 0;
            SetMask v = value;

            while ((v & static_cast<SetMask>(1)) == 0) {
                v >>= 1;
                ++index;
            }

            return index;
        }

        [[nodiscard]] static constexpr bool hasSingleBit(const SetMask value) {
            return value != 0 && (value & (value - 1)) == 0;
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
