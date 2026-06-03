#pragma once

#include "rbac/permission/template/ModuleSet.hpp"
#include "rbac/permission/template/Traits.hpp"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace vh::rbac::permission {
    namespace admin {
        enum class S3GatewayPermissions : uint8_t {
            None = 0,
            View = 1 << 0,
            ManageService = 1 << 1,
            ManageCredentials = 1 << 2,
            AssignPrincipal = 1 << 3,
            ManageBuckets = 1 << 4,
            ManageBudgets = 1 << 5,
            All = View | ManageService | ManageCredentials | AssignPrincipal | ManageBuckets | ManageBudgets
        };
    }

    template<>
    struct PermissionTraits<admin::S3GatewayPermissions> {
        using Entry = PermissionEntry<admin::S3GatewayPermissions>;

        static constexpr std::array entries{
            Entry{admin::S3GatewayPermissions::View, "view", "Allows viewing S3 gateway status and metadata."},
            Entry{admin::S3GatewayPermissions::ManageService, "manage_service", "Allows managing the S3 gateway service."},
            Entry{admin::S3GatewayPermissions::ManageCredentials, "manage_credentials", "Allows managing S3 gateway credentials."},
            Entry{admin::S3GatewayPermissions::AssignPrincipal, "assign_principal", "Allows assigning S3 gateway credentials to another principal."},
            Entry{admin::S3GatewayPermissions::ManageBuckets, "manage_buckets", "Allows managing S3 gateway bucket bindings."},
            Entry{admin::S3GatewayPermissions::ManageBudgets, "manage_budgets", "Allows managing S3 gateway budgets."},
        };
    };

    namespace admin {
        struct S3Gateway final : ModuleSet<uint8_t, S3GatewayPermissions, uint8_t> {
            static constexpr const auto* FLAG_CONTEXT = "s3-gateway";

            S3Gateway() = default;
            explicit S3Gateway(const Mask& mask) : ModuleSet(mask) {}

            [[nodiscard]] const char* name() const override { return FLAG_CONTEXT; }
            [[nodiscard]] const char* flagPrefix() const override { return FLAG_CONTEXT; }
            [[nodiscard]] Mask toMask() const override;
            void fromMask(Mask mask) override;
            [[nodiscard]] std::string toFlagsString() const override;
            [[nodiscard]] std::vector<std::string> getFlags() const override { return getFlagsWithOwn(); }
            [[nodiscard]] PackedPermissionExportT<Mask> exportPermissions() const {
                return packAndExportWithOwn("admin.s3_gateway");
            }
            [[nodiscard]] std::string toString(uint8_t indent) const override;

            [[nodiscard]] bool canView() const noexcept { return has(S3GatewayPermissions::View); }
            [[nodiscard]] bool canManageService() const noexcept { return has(S3GatewayPermissions::ManageService); }
            [[nodiscard]] bool canManageCredentials() const noexcept { return has(S3GatewayPermissions::ManageCredentials); }
            [[nodiscard]] bool canAssignPrincipal() const noexcept { return has(S3GatewayPermissions::AssignPrincipal); }
            [[nodiscard]] bool canManageBuckets() const noexcept { return has(S3GatewayPermissions::ManageBuckets); }
            [[nodiscard]] bool canManageBudgets() const noexcept { return has(S3GatewayPermissions::ManageBudgets); }

            static S3Gateway None() {
                S3Gateway s;
                s.clear();
                return s;
            }

            static S3Gateway ViewOnly() {
                S3Gateway s;
                s.clear();
                s.grant(S3GatewayPermissions::View);
                return s;
            }

            static S3Gateway Operator() {
                S3Gateway s;
                s.clear();
                s.grant(S3GatewayPermissions::View);
                s.grant(S3GatewayPermissions::ManageService);
                s.grant(S3GatewayPermissions::ManageBuckets);
                s.grant(S3GatewayPermissions::ManageBudgets);
                return s;
            }

            static S3Gateway CredentialManager() {
                S3Gateway s;
                s.clear();
                s.grant(S3GatewayPermissions::View);
                s.grant(S3GatewayPermissions::ManageCredentials);
                return s;
            }

            static S3Gateway PrincipalAssigner() {
                S3Gateway s = CredentialManager();
                s.grant(S3GatewayPermissions::AssignPrincipal);
                return s;
            }

            static S3Gateway Full() {
                S3Gateway s;
                s.clear();
                s.grant(S3GatewayPermissions::All);
                return s;
            }
        };

        void to_json(nlohmann::json& j, const S3Gateway& s);
        void from_json(const nlohmann::json& j, S3Gateway& s);
    }
}
