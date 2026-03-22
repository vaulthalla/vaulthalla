#pragma once

#include "rbac/permission/template/Module.hpp"
#include "Description.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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

        [[nodiscard]] std::vector<std::string> getFlags() const override = 0;

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

        [[nodiscard]] std::string toBitString() const override {
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

        [[nodiscard]] static const typename PermissionTraits<Enum>::Entry *findEntry(std::string_view slug)
            requires HasPermissionTraits<Enum> {
            for (const auto &entry: PermissionTraits<Enum>::entries)
                if (entry.slug == slug) return &entry;

            return nullptr;
        }

        bool applyFlag(const std::string_view flag)
            requires HasPermissionTraits<Enum> {
            const auto allowAlias = "--" + std::string(ALLOW_FLAG_ALIAS);
            const auto denyAlias = "--" + std::string(DENY_FLAG_ALIAS);

            if (!flag.starts_with(allowAlias) || !flag.starts_with(denyAlias)) {
                const auto bare = flag.substr(2);
                if (const auto *entry = findEntry(bare)) {
                    grant(entry->value);
                    return true;
                }
            }

            auto size = allowAlias.size();
            if (flag.starts_with(allowAlias)) {
                const auto slug = flag.substr(size);
                if (const auto *entry = findEntry(slug)) {
                    grant(entry->value);
                    return true;
                }
                return false;
            }

            size = denyAlias.size();
            if (flag.starts_with(denyAlias)) {
                const auto slug = flag.substr(size);
                if (const auto *entry = findEntry(slug)) {
                    revoke(entry->value);
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

        [[nodiscard]] std::string ownFlagsString() const
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

        template<typename... PermissionTs>
        [[nodiscard]] std::string joinFlagsWithOwn(const PermissionTs &... p) const {
            std::ostringstream oss;

            auto append = [&](const auto &permission) {
                const auto flags = permission.toFlagsString();
                if (flags.empty()) return;

                oss << ' ';
                oss << flags;
            };

            (void) append;

            oss << ownFlagsString();
            (append(p), ...);
            return oss.str();
        }

        [[nodiscard]] std::vector<std::string> ownFlags() const {
            std::vector<std::string> flags;

            const auto basePrefix = std::format("--{}-", flagPrefix()); // implicit allow
            const auto allowFullPrefix = std::format("--{}-{}-", ALLOW_FLAG_ALIAS, flagPrefix());
            const auto denyFullPrefix = std::format("--{}-{}-", DENY_FLAG_ALIAS, flagPrefix());

            for (const auto &entry: PermissionTraits<Enum>::entries) {
                flags.emplace_back(basePrefix + std::string(entry.slug));
                flags.emplace_back(allowFullPrefix + std::string(entry.slug));
                flags.emplace_back(denyFullPrefix + std::string(entry.slug));
            }

            return flags;
        }

        template<typename... PermissionTs>
        [[nodiscard]] std::vector<std::string> getFlagsWithOwn(const PermissionTs &... p) const {
            std::vector<std::string> flags = ownFlags();

            auto append = [&](const auto &permission) {
                for (const auto &flag: permission.getFlags()) flags.emplace_back(flag);
            };

            (void) append;

            (append(p), ...);
            return flags;
        }

        template<typename... SetTs>
        [[nodiscard]] std::vector<ResolvedFlagBinding> getFlagBindingsWithOwn(SetTs &... sets)
            requires HasPermissionTraits<Enum> {
            std::vector<ResolvedFlagBinding> bindings;

            appendResolvedBindings(bindings, *this);
            (appendResolvedBindings(bindings, sets), ...);

            return bindings;
        }

        template<typename SetT>
        static void appendResolvedBindings(
            std::vector<ResolvedFlagBinding> &out,
            SetT &set
        ) {
            for (const auto &binding: set.getFlagBindings()) {
                out.push_back({
                    .flag = binding.flag,
                    .apply = [&set, binding]() {
                        if (binding.operation == FlagOperation::Grant) set.grant(binding.permission);
                        else set.revoke(binding.permission);
                    }
                });
            }
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
