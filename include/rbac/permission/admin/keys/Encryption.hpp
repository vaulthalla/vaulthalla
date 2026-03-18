#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace admin::keys {
        enum class EncryptionKeyPermissions : uint8_t {
            None = 0,
            View = 1 << 0,
            Export = 1 << 1,
            Rotate = 1 << 2,
            All = View | Export | Rotate
        };
    }

    template<>
    struct PermissionTraits<admin::keys::EncryptionKeyPermissions> {
        using Entry = PermissionEntry<admin::keys::EncryptionKeyPermissions>;

        static constexpr std::array entries{
            Entry{
                admin::keys::EncryptionKeyPermissions::View,
                "view",
                "Allows viewing encryption key details, excluding the key material."
            },
            Entry{
                admin::keys::EncryptionKeyPermissions::Export,
                "export",
                "Allows exporting the encryption key material."
            },
            Entry{
                admin::keys::EncryptionKeyPermissions::Rotate,
                "rotate",
                "Allows rotating the encryption key, which generates a new key and deprecates the old one."
            }
        };
    };

    namespace admin::keys {
        struct EncryptionKey final : Set<EncryptionKeyPermissions, uint8_t> {
            static constexpr const auto* FLAG_CONTEXT = "encryption-key";

            [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(EncryptionKeyPermissions::View); }
            [[nodiscard]] bool canExport() const noexcept { return has(EncryptionKeyPermissions::Export); }
            [[nodiscard]] bool canRotate() const noexcept { return has(EncryptionKeyPermissions::Rotate); }

            static EncryptionKey None() {
                EncryptionKey k;
                k.clear();
                return k;
            }

            static EncryptionKey ViewOnly() {
                EncryptionKey k;
                k.clear();
                k.grant(EncryptionKeyPermissions::View);
                return k;
            }

            static EncryptionKey Exporter() {
                EncryptionKey k;
                k.clear();
                k.grant(EncryptionKeyPermissions::View);
                k.grant(EncryptionKeyPermissions::Export);
                return k;
            }

            static EncryptionKey Rotator() {
                EncryptionKey k;
                k.clear();
                k.grant(EncryptionKeyPermissions::View);
                k.grant(EncryptionKeyPermissions::Rotate);
                return k;
            }

            static EncryptionKey Custodian() {
                EncryptionKey k;
                k.clear();
                k.grant(EncryptionKeyPermissions::View);
                k.grant(EncryptionKeyPermissions::Export);
                k.grant(EncryptionKeyPermissions::Rotate);
                return k;
            }

            static EncryptionKey Full() {
                EncryptionKey k;
                k.clear();
                k.grant(EncryptionKeyPermissions::All);
                return k;
            }

            static EncryptionKey Custom(const Mask mask) {
                EncryptionKey k;
                k.setRaw(mask);
                return k;
            }
        };

        void to_json(nlohmann::json& j, const EncryptionKey& k);
        void from_json(const nlohmann::json& j, EncryptionKey& k);
    }
}
