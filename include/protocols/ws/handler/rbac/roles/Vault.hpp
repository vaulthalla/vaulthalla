#pragma once

#include "protocols/ws/Session.hpp"
#include "rbac/vault/resolver/*.hpp"

#include <nlohmann/json_fwd.hpp>
#include <memory>

namespace vh::protocols::ws { class Session; }

namespace vh::protocols::ws::handler::rbac::roles {

    using json = nlohmann::json;

    struct Vault {
        static json add(const json& payload, const std::shared_ptr<Session>& session);
        static json remove(const json& payload, const std::shared_ptr<Session>& session);
        static json update(const json& payload, const std::shared_ptr<Session>& session);
        static json get(const json& payload, const std::shared_ptr<Session>& session);
        static json getByName(const json& payload, const std::shared_ptr<Session>& session);
        static json list(const std::shared_ptr<Session>& session);
        static json assign(const json& payload, const std::shared_ptr<Session>& session);
        static json unassign(const json& payload, const std::shared_ptr<Session>& session);


    private:
        template<typename EnumT>
    static void enforcePermission(const std::shared_ptr<Session>& session,
                                  const uint32_t vaultId,
                                  const EnumT permission,
                                  const std::string_view error = "Permission denied") {
            if (!session || !session->user)
                throw std::runtime_error("Unauthorized");

            if (!vh::rbac::vault::Resolver::has<EnumT>({
                .user = session->user,
                .permission = permission,
                .vault_id = vaultId,
            })) throw std::runtime_error(std::string(error));
        }

        template<typename EnumT, typename... EnumTs>
        static void enforceAnyPermission(const std::shared_ptr<Session>& session,
                                         const uint32_t vaultId,
                                         const std::filesystem::path& path,
                                         const EnumT permission,
                                         const EnumTs... permissions) {
            if (!session || !session->user)
                throw std::runtime_error("Unauthorized");

            if (!rbac::vault::Resolver::hasAny(
                rbac::vault::Context<EnumT>{
                    .user = session->user,
                    .permission = permission,
                    .vault_id = vaultId,
                    .path = path
                },
                rbac::vault::Context<EnumTs>{
                    .user = session->user,
                    .permission = permissions,
                    .vault_id = vaultId,
                    .path = path
                }...
            )) throw std::runtime_error("Permission denied: Required permission not granted");
        }
    };

}
