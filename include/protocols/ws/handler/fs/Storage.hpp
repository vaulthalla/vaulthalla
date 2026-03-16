#pragma once

#include "protocols/ws/Session.hpp"
#include "rbac/resolver/vault/*.hpp"

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace vh::protocols::ws::handler::fs {

using json = nlohmann::json;

class Storage {
public:
    static json startUpload(const json& payload, const std::shared_ptr<Session>& session);

    static json finishUpload(const json& payload, const std::shared_ptr<Session>& session);

    static json mkdir(const json& payload, const std::shared_ptr<Session>& session);

    static json move(const json& payload, const std::shared_ptr<Session>& session);

    static json rename(const json& payload, const std::shared_ptr<Session>& session);

    static json copy(const json& payload, const std::shared_ptr<Session>& session);

    static json listDir(const json& payload, const std::shared_ptr<Session>& session);

    static json remove(const json& payload, const std::shared_ptr<Session>& session);

private:
    template<typename EnumT>
    static void enforcePermission(const std::shared_ptr<Session>& session,
                                  const uint32_t vaultId,
                                  const std::filesystem::path& path,
                                  const EnumT permission,
                                  const std::string_view error = "Permission denied") {
        if (!session || !session->user)
            throw std::runtime_error("Unauthorized");

        if (!resolver::Vault::has<EnumT>({
            .user = session->user,
            .permission = permission,
            .vault_id = vaultId,
            .path = path
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

        if (!resolver::Vault::hasAny(
            resolver::vault::Context<EnumT>{
                .user = session->user,
                .permission = permission,
                .vault_id = vaultId,
                .path = path
            },
            resolver::vault::Context<EnumTs>{
                .user = session->user,
                .permission = permissions,
                .vault_id = vaultId,
                .path = path
            }...
        )) throw std::runtime_error("Permission denied: Required permission not granted");
    }
};

}
