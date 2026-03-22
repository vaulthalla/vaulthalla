#pragma once

#include "Traits.hpp"
#include "Meta.hpp"
#include "Description.hpp"
#include "Flag.hpp"
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

        [[nodiscard]] static const PermissionTraits<Enum>::Entry *findEntry(std::string_view slug)
            requires HasPermissionTraits<Enum> {
            for (const auto &entry: PermissionTraits<Enum>::entries)
                if (entry.slug == slug) return &entry;

            return nullptr;
        }

        [[nodiscard]] std::vector<FlagBinding<Enum> > getFlagBindings() const
            requires HasPermissionTraits<Enum> {
            std::vector<FlagBinding<Enum> > bindings;
            bindings.reserve(PermissionTraits<Enum>::entries.size() * 3);

            const auto basePrefix = std::format("--{}-", flagPrefix());
            const auto allowFullPrefix = std::format("--{}-{}-", ALLOW_FLAG_ALIAS, flagPrefix());
            const auto denyFullPrefix = std::format("--{}-{}-", DENY_FLAG_ALIAS, flagPrefix());

            for (const auto &entry: PermissionTraits<Enum>::entries) {
                bindings.push_back({
                    .flag = basePrefix + std::string(entry.slug),
                    .permission = entry.value,
                    .operation = FlagOperation::Grant,
                    .slug = entry.slug
                });

                bindings.push_back({
                    .flag = allowFullPrefix + std::string(entry.slug),
                    .permission = entry.value,
                    .operation = FlagOperation::Grant,
                    .slug = entry.slug
                });

                bindings.push_back({
                    .flag = denyFullPrefix + std::string(entry.slug),
                    .permission = entry.value,
                    .operation = FlagOperation::Revoke,
                    .slug = entry.slug
                });
            }

            return bindings;
        }

        [[nodiscard]] std::vector<std::string> getFlags() const
            requires HasPermissionTraits<Enum> {
            std::vector<std::string> flags;
            const auto bindings = getFlagBindings();

            flags.reserve(bindings.size());
            for (const auto &binding: bindings)
                flags.push_back(binding.flag);

            return flags;
        }

        [[nodiscard]] bool applyFlag(const std::string_view flag)
            requires HasPermissionTraits<Enum> {
            for (const auto &binding: getFlagBindings()) {
                if (binding.flag != flag) continue;

                if (binding.operation == FlagOperation::Grant) grant(binding.permission);
                else revoke(binding.permission);

                return true;
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

                std::vector<std::string> flags;
                flags.reserve(3);

                const auto dashed = std::string(qualifiedPrefix);
                const auto slug = std::string(entry.slug);

                flags.emplace_back(std::format("--{}-{}", dashed, slug));
                flags.emplace_back(std::format("--{}-{}-{}", ALLOW_FLAG_ALIAS, dashed, slug));
                flags.emplace_back(std::format("--{}-{}-{}", DENY_FLAG_ALIAS, dashed, slug));

                *out++ = Permission{
                    static_cast<uint32_t>(globalBit),
                    std::move(qualifiedName),
                    buildPermissionDescription(describer, entry.description, descriptionPrefix),
                    flags,
                    static_cast<uint64_t>(raw),
                    typeid(Enum)
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
