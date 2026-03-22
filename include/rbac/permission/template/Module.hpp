#pragma once

#include "rbac/permission/template/Set.hpp"
#include "Export.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace vh::rbac::permission {
    template<typename MaskT>
    struct Module {
        using Mask = MaskT;
        using Offset = std::size_t;

        static_assert(std::is_integral_v<Mask>, "Module<MaskT>: MaskT must be an integral type");
        static_assert(std::is_unsigned_v<Mask>, "Module<MaskT>: MaskT must be an unsigned integral type");

        virtual ~Module() = default;

        Module() = default;

        [[nodiscard]] static constexpr std::size_t bitWidth() {
            return sizeof(Mask) * 8u;
        }

        [[nodiscard]] virtual const char *name() const = 0;

        [[nodiscard]] virtual Mask toMask() const = 0;

        virtual void fromMask(Mask mask) = 0;

        [[nodiscard]] virtual std::string toFlagsString() const = 0;

        virtual std::vector<std::string> getFlags() const = 0;

        [[nodiscard]] virtual std::string toString(uint8_t indent) const = 0;

        [[nodiscard]] virtual std::string toBitString() const {
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

        template<typename... PermissionTs>
        [[nodiscard]] std::vector<std::string> getFlags(const PermissionTs &... p) const {
            std::vector<std::string> flags;

            auto append = [&](const auto &permission) {
                for (const auto &perm: permission.getFlags()) flags.push_back(perm);
            };

            (void) append;

            (append(p), ...);
            return flags;
        }

    protected:
        template<SetLike SetLikeT>
        static void append(Mask &dstMask, Offset &offset, const SetLikeT &set, const char *context) {
            constexpr auto setBits = SetLikeT::bitWidth();

            if (offset + setBits > bitWidth())
                throw std::runtime_error(std::string(context) + ": bit width exceeds destination mask");

            dstMask |= static_cast<Mask>(set.raw()) << offset;
            offset += setBits;
        }

        template<SetLike SetLikeT>
        static void extract(const Mask srcMask, Offset &offset, SetLikeT &set, const char *context) {
            constexpr auto setBits = SetLikeT::bitWidth();

            if (offset + setBits > bitWidth())
                throw std::runtime_error(std::string(context) + ": bit width exceeds source mask");

            constexpr Mask fieldMask =
                    setBits >= bitWidth()
                        ? static_cast<Mask>(~static_cast<Mask>(0))
                        : static_cast<Mask>((static_cast<Mask>(1) << setBits) - 1);

            using RawT = decltype(set.raw());
            const auto value = static_cast<RawT>((srcMask >> offset) & fieldMask);
            set.setRaw(value);

            offset += setBits;
        }

        template<SetLike... SetTs>
        [[nodiscard]] Mask pack(const SetTs &... sets) const {
            Mask mask = 0;
            Offset offset = 0;
            (append(mask, offset, sets, name()), ...);
            return mask;
        }

        template<SetLike... SetTs>
        void unpack(const Mask srcMask, SetTs &... sets) const {
            Offset offset = 0;
            (extract(srcMask, offset, sets, name()), ...);
        }

        template<typename... PermissionTs>
        [[nodiscard]] static std::string joinFlags(const PermissionTs &... permissions) {
            std::ostringstream oss;
            bool first = true;

            auto append = [&](const auto &permission) {
                const auto flags = permission.toFlagsString();
                if (flags.empty()) return;

                if (!first) oss << ' ';
                first = false;
                oss << flags;
            };

            (append(permissions), ...);
            return oss.str();
        }

        template<ExportableSetLike SetT>
        static void appendAndExport(
            Mask &dstMask,
            Offset &offset,
            const MountedSetRef<SetT> &mounted,
            const char *context,
            std::vector<Permission> &out
        ) {
            constexpr auto setBits = SetT::bitWidth();

            if (offset + setBits > bitWidth())
                throw std::runtime_error(std::string(context) + ": bit width exceeds destination mask");

            dstMask |= static_cast<Mask>(mounted.set.raw()) << offset;
            mounted.set.exportPermissions(mounted.set, std::back_inserter(out), offset, mounted.qualifiedName);
            offset += setBits;
        }

        template<ExportableSetLike... SetTs>
        [[nodiscard]] PackedPermissionExportT<Mask> packAndExportPerms(const MountedSetRef<SetTs> &... sets) const {
            PackedPermissionExportT<Mask> result{};
            Offset offset = 0;
            (appendAndExport(result.mask, offset, sets, name(), result.permissions), ...);
            return result;
        }

        [[nodiscard]] static std::string bool_to_string(const bool value) {
            return value ? "true" : "false";
        }
    };
}
