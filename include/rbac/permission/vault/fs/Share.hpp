#pragma once

#include "rbac/permission/template/Set.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace vault::fs {
        enum class SharePermissions : uint8_t {
            None = 0,
            Internal = 1 << 0,
            Public = 1 << 1,
            PublicWithValidation = 1 << 2,
            All = Internal | Public | PublicWithValidation
        };
    }

    template<>
    struct PermissionTraits<vault::fs::SharePermissions> {
        using Entry = PermissionEntry<vault::fs::SharePermissions>;

        static constexpr std::array entries{
            Entry{
                vault::fs::SharePermissions::Internal,
                "internal",
                "Allows sharing of with other users within the same vault."
            },
            Entry{
                vault::fs::SharePermissions::Public,
                "public",
                "Allows sharing with users outside the vault without validation."
            },
            Entry{
                vault::fs::SharePermissions::PublicWithValidation,
                "public_with_val",
                "Allows public sharing with validation by a trusted authority."
            },
        };
    };

    namespace vault::fs {
        struct Share final : Set<SharePermissions, uint8_t> {
            std::string module_prefix;

            Share() = default;
            explicit Share(const Mask& mask) : Set(mask) {}
            explicit Share(const std::string& prefix) : module_prefix(prefix + "-share") {}

            [[nodiscard]] const char* flagPrefix() const override { return module_prefix.c_str(); }

            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canShareInternally() const noexcept {
                return has(SharePermissions::Internal);
            }

            [[nodiscard]] bool canSharePublicly() const noexcept {
                return has(SharePermissions::Public);
            }

            [[nodiscard]] bool canSharePubliclyWithValidation() const noexcept {
                return has(SharePermissions::PublicWithValidation);
            }

            [[nodiscard]] bool all() const noexcept {
                return raw() == static_cast<Mask>(SharePermissions::All);
            }

            [[nodiscard]] bool none() const noexcept {
                return raw() == 0;
            }

            [[nodiscard]] bool hasAnyPublicSharing() const noexcept {
                return canSharePublicly() || canSharePubliclyWithValidation();
            }

            [[nodiscard]] bool hasAll(const SharePermissions perms) const noexcept {
                const auto mask = static_cast<Mask>(perms);
                return (raw() & mask) == mask;
            }

            static Share None() {
                Share s;
                s.clear();
                return s;
            }

            static Share InternalOnly() {
                Share s;
                s.clear();
                s.grant(SharePermissions::Internal);
                return s;
            }

            static Share PublicOnly() {
                Share s;
                s.clear();
                s.grant(SharePermissions::Public);
                return s;
            }

            static Share PublicValidatedOnly() {
                Share s;
                s.clear();
                s.grant(SharePermissions::PublicWithValidation);
                return s;
            }

            static Share InternalAndPublic() {
                Share s;
                s.clear();
                s.grant(SharePermissions::Internal);
                s.grant(SharePermissions::Public);
                return s;
            }

            static Share InternalAndValidatedPublic() {
                Share s;
                s.clear();
                s.grant(SharePermissions::Internal);
                s.grant(SharePermissions::PublicWithValidation);
                return s;
            }

            static Share PublicAll() {
                Share s;
                s.clear();
                s.grant(SharePermissions::Public);
                s.grant(SharePermissions::PublicWithValidation);
                return s;
            }

            static Share Full() {
                Share s;
                s.clear();
                s.grant(SharePermissions::All);
                return s;
            }
        };

        void to_json(nlohmann::json& j, const Share& s);
        void from_json(const nlohmann::json& j, Share& s);
    }
}
