#pragma once

#include "Traits.hpp"
#include "Meta.hpp"
#include "Description.hpp"
#include "rbac/permission/Permission.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace vh::rbac::permission {
    template<typename EnumT, typename MaskT>
    struct Set {
        using Enum = EnumT;
        using Mask = MaskT;

        static_assert(std::is_integral_v<Mask>, "Set<EnumT, MaskT>: MaskT must be an integral type");
        static_assert(std::is_unsigned_v<Mask>, "Set<EnumT, MaskT>: MaskT must be an unsigned integral type");

        Mask permissions{0};

        virtual ~Set() = default;

        Set() = default;

        explicit Set(const Mask mask) : permissions(mask) {
        }

        [[nodiscard]] virtual const char *flagPrefix() const = 0;

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

        [[nodiscard]] static const typename PermissionTraits<Enum>::Entry *findEntry(std::string_view slug)
            requires HasPermissionTraits<Enum> {
            for (const auto &entry: PermissionTraits<Enum>::entries)
                if (entry.slug == slug) return &entry;

            return nullptr;
        }

        bool applyFlag(const std::string_view flag)
            requires HasPermissionTraits<Enum> {
            auto size = std::string(ALLOW_FLAG_ALIAS).size();
            if (flag.starts_with(ALLOW_FLAG_ALIAS)) {
                const auto slug = flag.substr(size);
                if (const auto *entry = findEntry(slug)) {
                    grant(entry->value);
                    return true;
                }
                return false;
            }

            size = std::string(DENY_FLAG_ALIAS).size();
            if (flag.starts_with(DENY_FLAG_ALIAS)) {
                const auto slug = flag.substr(size);
                if (const auto *entry = findEntry(slug)) {
                    revoke(entry->value);
                    return true;
                }
                return false;
            }

            return false;
        }

        [[nodiscard]] std::string toFlagsString() const
            requires HasPermissionTraits<Enum> {
            std::ostringstream oss;
            bool first = true;

            const auto allowFullPrefix = std::format("--{}-{}-", ALLOW_FLAG_ALIAS, flagPrefix());
            const auto denyFullPrefix = std::format("--{}-{}-", DENY_FLAG_ALIAS, flagPrefix());

            for (const auto &entry: PermissionTraits<Enum>::entries) {
                if (!first) oss << ' ';
                first = false;

                oss << (has(entry.value) ? allowFullPrefix : denyFullPrefix) << entry.slug;
            }

            return oss.str();
        }

        [[nodiscard]] virtual std::string toString(uint8_t indent) const = 0;

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
                const auto raw = static_cast<Mask>(entry.value);
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
        [[nodiscard]] static std::string bool_to_string(const bool value) {
            return value ? "true" : "false";
        }

        [[nodiscard]] static constexpr uint32_t singleBitIndex(const Mask value) {
            uint32_t index = 0;
            Mask v = value;

            while ((v & static_cast<Mask>(1)) == 0) {
                v >>= 1;
                ++index;
            }

            return index;
        }

        [[nodiscard]] static constexpr bool hasSingleBit(const Mask value) {
            return value != 0 && (value & (value - 1)) == 0;
        }
    };
}
