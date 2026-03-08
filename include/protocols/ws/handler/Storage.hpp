#pragma once

#include "protocols/ws/Session.hpp"
#include "identities/model/User.hpp"

#include <memory>
#include <nlohmann/json.hpp>

namespace vh::protocols::ws::handler {

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
    template <typename... Funcs> static void enforcePermissions(
        const std::shared_ptr<Session>& session,
        const unsigned int vaultId,
        const std::filesystem::path& path, // Add path param
        Funcs... checks) {
        if (!session->user) throw std::runtime_error("Unauthorized");

        if (session->user->isAdmin()) return;

        const auto role = session->user->getRole(vaultId);
        if (!role) throw std::runtime_error("No role assigned for this vault/volume");

        if (!( ((role.get()->*checks)(path)) || ... )) {
            throw std::runtime_error("Permission denied: Required permission not granted");
        }
    }

};

}
