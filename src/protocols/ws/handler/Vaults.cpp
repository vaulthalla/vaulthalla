#include "protocols/ws/handler/Vaults.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/model/User.hpp"
#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "db/query/vault/Vault.hpp"
#include "storage/Manager.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "sync/Controller.hpp"

#include <nlohmann/json.hpp>
#include <boost/algorithm/string.hpp>

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::sync::model;
using json = nlohmann::json;

json Vaults::add(const json& payload, const std::shared_ptr<Session>& session) {
    const std::string name = payload.at("name").get<std::string>();
    const std::string type = payload.at("type").get<std::string>();
    const std::string typeLower = boost::algorithm::to_lower_copy(type);
    const std::string mountPoint = payload.at("mount_point").get<std::string>();

    std::shared_ptr<Vault> vault;
    std::shared_ptr<Policy> sync = nullptr;

    if (typeLower == "s3") {
        const auto apiKeyID = payload.at("api_key_id").get<unsigned int>();
        const std::string bucket = payload.at("bucket").get<std::string>();
        vault = std::make_shared<S3Vault>(name, apiKeyID, bucket);
        sync = std::make_shared<RemotePolicy>(payload);
    }

    vault->name = name;
    vault->mount_point = mountPoint;
    vault->owner_id = session->user->id;

    vault = runtime::Deps::get().storageManager->addVault(vault, sync);

    return {{"vault", *vault}};
}

json Vaults::update(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vault = std::make_shared<Vault>(payload);
    if (!session->user->canManageVault(vault->id)) throw std::runtime_error("User does not have permission to update this vault.");

    runtime::Deps::get().storageManager->updateVault(vault);
    return {{"vault", *vault}};
}

json Vaults::remove(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("id").get<unsigned int>();
    const auto vault = runtime::Deps::get().storageManager->getVault(vaultId);

    if (!session->user->isAdmin() && (!session->user->canManageVaults() || !session->user->canManageVault(vaultId)))
        throw std::runtime_error("User does not have permission to delete vaults.");

    runtime::Deps::get().storageManager->removeVault(vaultId);
    return {};
}

json Vaults::get(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("id").get<unsigned int>();
    if (!session->user->canManageVault(vaultId)) throw std::runtime_error("User does not have permission to get vaults.");

    const auto vault = runtime::Deps::get().storageManager->getVault(vaultId);
    if (!vault) throw std::runtime_error("Vault not found with ID: " + std::to_string(vaultId));

    json data = {};

    if (vault->type == VaultType::S3) {
        const auto s3Vault = std::static_pointer_cast<S3Vault>(vault);
        data["vault"] = *s3Vault;
    } else data["vault"] = *vault;

    if (vault->owner_id == session->user->id) data["vault"]["owner"] = session->user->name;
    else data["vault"]["owner"] = db::query::vault::Vault::getVaultOwnersName(vaultId);

    return data;
}

json Vaults::list(const std::shared_ptr<Session>& session) {
    return session->user->canManageVaults() ?
        json{{"vaults", db::query::vault::Vault::listVaults()}} :
        json{{"vaults", db::query::vault::Vault::listUserVaults(session->user->id)}};
}

json Vaults::sync(const json& payload, const std::shared_ptr<Session>& session) {
    const auto vaultId = payload.at("id").get<unsigned int>();
    if (!session->user->canSyncVaultData(vaultId)) throw std::runtime_error("User does not have permission to sync vaults.");
    runtime::Deps::get().syncController->runNow(vaultId);
    return {};
}
