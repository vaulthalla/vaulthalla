#pragma once

#include "rbac/permission/template/ModuleSet.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace admin {
        enum class AuditPermissions : uint8_t {
            None = 0,
            View = 1 << 0,
            All = View
        };
    }

    template<>
    struct PermissionTraits<admin::AuditPermissions> {
        using Entry = PermissionEntry<admin::AuditPermissions>;

        static constexpr std::array entries{
            Entry{admin::AuditPermissions::View, "view", "Allows viewing of audit logs and database objects."},
        };
    };

    namespace admin {
        struct Audits final : ModuleSet<uint8_t, AuditPermissions, uint8_t> {
            static constexpr const auto *FLAG_CONTEXT = "audit";

            Audits() = default;

            explicit Audits(const Mask &mask) : ModuleSet(mask) {}

            [[nodiscard]] const char *name() const override { return FLAG_CONTEXT; }
            [[nodiscard]] const char *flagPrefix() const override { return FLAG_CONTEXT; }
            [[nodiscard]] Mask toMask() const override;
            void fromMask(Mask mask) override;
            [[nodiscard]] std::string toFlagsString() const override;

            [[nodiscard]] std::vector<std::string> getFlags() const override {
                return getFlagsWithOwn();
            }

            [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
                return packAndExportWithOwn("admin.audits");
            }

            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(AuditPermissions::View); }

            static Audits None() {
                Audits a;
                a.clear();
                return a;
            }

            static Audits ViewOnly() {
                Audits a;
                a.clear();
                a.grant(AuditPermissions::View);
                return a;
            }

            static Audits Full() {
                Audits a;
                a.clear();
                a.grant(AuditPermissions::All);
                return a;
            }
        };

        void to_json(nlohmann::json &j, const Audits &a);

        void from_json(const nlohmann::json &j, Audits &a);
    }
}
