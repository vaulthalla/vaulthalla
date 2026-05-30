#include "protocols/ws/handler/vault/Vaults.hpp"
#include "vault/model/APIKey.hpp"
#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/sync/Policy.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/encoding/interval.hpp"
#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "protocols/ws/Session.hpp"
#include "runtime/Deps.hpp"
#include "sync/Controller.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "rbac/permission/admin/Keys.hpp"
#include "rbac/permission/admin/Vaults.hpp"

#include <nlohmann/json.hpp>
#include <boost/algorithm/string.hpp>

#include <chrono>
#include <mutex>
#include <sstream>

using namespace vh::protocols::ws::handler;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::sync::model;
using namespace vh::rbac;
using json = nlohmann::json;

namespace {
    std::chrono::seconds parsePolicyInterval(const json& value) {
        if (value.is_number_integer()) return Policy::clampInterval(std::chrono::seconds(value.get<int64_t>()));
        if (!value.is_string()) throw std::runtime_error("sync.interval must be a string or number of seconds");

        const auto raw = value.get<std::string>();
        try {
            return Policy::clampInterval(vh::db::encoding::parseSyncInterval(raw));
        } catch (const std::exception&) {
            std::chrono::seconds total{0};
            std::stringstream ss(raw);
            std::string token;
            bool sawToken = false;
            while (ss >> token) {
                if (token == "0s" || token == "0m" || token == "0h" || token == "0d") continue;
                total += vh::db::encoding::parseSyncInterval(token);
                sawToken = true;
            }
            if (!sawToken) throw;
            return Policy::clampInterval(total);
        }
    }

    std::optional<uint64_t> parseBudgetValue(const json& value) {
        if (value.is_null()) return std::nullopt;
        if (!value.is_number_unsigned() && !value.is_number_integer())
            throw std::runtime_error("S3 request budget values must be numbers or null");
        if (value.is_number_integer() && value.get<int64_t>() < 0)
            throw std::runtime_error("S3 request budget values cannot be negative");
        return value.get<uint64_t>();
    }

    std::shared_ptr<RemotePolicy> loadRemotePolicy(const unsigned int vaultId) {
        if (const auto engine = vh::runtime::Deps::get().storageManager->getEngine(vaultId)) {
            if (const auto remote = std::dynamic_pointer_cast<RemotePolicy>(engine->sync)) return remote;
        }

        return std::dynamic_pointer_cast<RemotePolicy>(vh::db::query::sync::Policy::getSync(vaultId));
    }

    void applyRemotePolicyPatch(RemotePolicy& sync, const json& patch) {
        if (patch.contains("interval")) sync.interval = parsePolicyInterval(patch.at("interval"));
        if (patch.contains("enabled")) sync.enabled = patch.at("enabled").get<bool>();
        if (patch.contains("strategy")) sync.strategy = strategyFromString(patch.at("strategy").get<std::string>());
        if (patch.contains("conflict_policy"))
            sync.conflict_policy = rsConflictPolicyFromString(patch.at("conflict_policy").get<std::string>());
        if (patch.contains("max_remote_index_age_seconds")) {
            if (patch.at("max_remote_index_age_seconds").is_null()) sync.max_remote_index_age = std::nullopt;
            else {
                const auto seconds = patch.at("max_remote_index_age_seconds").get<int64_t>();
                if (seconds < 0) throw std::runtime_error("sync.max_remote_index_age_seconds cannot be negative");
                sync.max_remote_index_age = std::chrono::seconds(seconds);
            }
        }

        if (patch.contains("s3_request_budget")) {
            const auto& budget = patch.at("s3_request_budget");
            if (!budget.is_object()) throw std::runtime_error("sync.s3_request_budget must be an object");
            if (budget.contains("list_requests"))
                sync.s3_request_budget.max_list_requests = parseBudgetValue(budget.at("list_requests"));
            if (budget.contains("head_requests"))
                sync.s3_request_budget.max_head_requests = parseBudgetValue(budget.at("head_requests"));
            if (budget.contains("get_requests"))
                sync.s3_request_budget.max_get_requests = parseBudgetValue(budget.at("get_requests"));
            if (budget.contains("put_requests"))
                sync.s3_request_budget.max_put_requests = parseBudgetValue(budget.at("put_requests"));
            if (budget.contains("copy_requests"))
                sync.s3_request_budget.max_copy_requests = parseBudgetValue(budget.at("copy_requests"));
            if (budget.contains("delete_requests"))
                sync.s3_request_budget.max_delete_requests = parseBudgetValue(budget.at("delete_requests"));
            if (budget.contains("downloaded_bytes"))
                sync.s3_request_budget.max_downloaded_bytes = parseBudgetValue(budget.at("downloaded_bytes"));
        }

        sync.interval = Policy::clampInterval(sync.interval);
        sync.rehash_config();
    }

    std::shared_ptr<RemotePolicy> patchedRemotePolicyForVault(const unsigned int vaultId, const json& patch) {
        auto existing = loadRemotePolicy(vaultId);
        if (!existing) throw std::runtime_error("S3 sync policy not found for vault ID: " + std::to_string(vaultId));

        auto updated = std::make_shared<RemotePolicy>(*existing);
        applyRemotePolicyPatch(*updated, patch);
        updated->id = existing->id;
        updated->vault_id = existing->vault_id ? existing->vault_id : vaultId;
        return updated;
    }

    void attachRemotePolicyJson(json& vaultJson, const unsigned int vaultId) {
        if (const auto sync = loadRemotePolicy(vaultId)) vaultJson["sync"] = *sync;
    }
}

json Vaults::add(const json &payload, const std::shared_ptr<Session> &session) {
    const std::string name = payload.at("name").get<std::string>();
    const std::string type = payload.at("type").get<std::string>();
    const std::string typeLower = boost::algorithm::to_lower_copy(type);
    const std::string mountPoint = payload.value("mount_point", "");
    const auto ownerId = payload.contains("owner_id")
                             ? std::make_optional(payload.at("owner_id").get<uint32_t>())
                             : session->user->id;

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::Create,
        .target_user_id = ownerId
    })) throw std::runtime_error("User does not have permission to add vault.");

    std::shared_ptr<Vault> vault;
    std::shared_ptr<Policy> sync = nullptr;

    if (typeLower == "s3") {
        const auto apiKeyID = payload.at("api_key_id").get<unsigned int>();

        if (!resolver::Admin::has<permission::admin::keys::APIPermissions>({
            .user = session->user,
            .permission = permission::admin::keys::APIPermissions::Consume,
            .api_key_id = apiKeyID
        })) throw std::runtime_error("User does not have permission to add this api-key to vault.");

        const std::string bucket = payload.at("bucket").get<std::string>();
        const auto s3Vault = std::make_shared<S3Vault>(name, apiKeyID, bucket);
        const auto apiKey = db::query::vault::APIKey::getAPIKey(apiKeyID);
        if (!apiKey) throw std::runtime_error("API key not found: " + std::to_string(apiKeyID));

        const auto requestedTier = payload.contains("storage_tier_id") && !payload.at("storage_tier_id").is_null()
            ? std::make_optional(payload.at("storage_tier_id").get<std::string>())
            : std::optional<std::string>{};
        const auto tier = storage::s3::provider::resolve(apiKey->provider)->normalizeStorageTier(requestedTier);
        if (!tier.ok) throw std::runtime_error(tier.error);
        s3Vault->storage_tier_id = tier.normalized_id;

        vault = s3Vault;
        const auto remote = std::make_shared<RemotePolicy>();
        if (payload.contains("sync")) applyRemotePolicyPatch(*remote, payload.at("sync"));
        else applyRemotePolicyPatch(*remote, payload);
        sync = remote;
    }

    vault->name = name;
    vault->mount_point = mountPoint;
    vault->owner_id = session->user->id;

    vault = runtime::Deps::get().storageManager->addVault(vault, sync);

    return {{"vault", *vault}};
}

json Vaults::update(const json &payload, const std::shared_ptr<Session> &session) {
    std::shared_ptr<Vault> vault;
    const auto type = from_string(payload.at("type").get<std::string>());
    if (type == VaultType::S3) {
        const auto s3Vault = std::make_shared<S3Vault>();
        from_json(payload, *s3Vault);
        const auto apiKey = db::query::vault::APIKey::getAPIKey(s3Vault->api_key_id);
        if (!apiKey) throw std::runtime_error("API key not found: " + std::to_string(s3Vault->api_key_id));

        const auto tier = storage::s3::provider::resolve(apiKey->provider)->normalizeStorageTier(s3Vault->storage_tier_id);
        if (!tier.ok) throw std::runtime_error(tier.error);
        s3Vault->storage_tier_id = tier.normalized_id;
        vault = s3Vault;
    } else {
        vault = std::make_shared<Vault>();
        from_json(payload, *vault);
    }

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::Edit,
        .vault_id = vault->id
    })) throw std::runtime_error("User does not have permission to update vault.");

    // TODO: pull a diff and apply per role vGlobal perms to changes

    std::shared_ptr<RemotePolicy> updatedSync;
    if (type == VaultType::S3 && payload.contains("sync") && payload.at("sync").is_object()) {
        updatedSync = patchedRemotePolicyForVault(vault->id, payload.at("sync"));
        db::query::vault::Vault::updateVaultSync(updatedSync, vault->type);

        if (const auto engine = runtime::Deps::get().storageManager->getEngine(vault->id)) {
            std::unique_lock lock(engine->mutex);
            engine->sync = updatedSync;
        }
    }

    runtime::Deps::get().storageManager->updateVault(vault);
    if (updatedSync && runtime::Deps::get().syncController)
        runtime::Deps::get().syncController->refreshEngines();
    return {{"vault", *vault}};
}

json Vaults::remove(const json &payload, const std::shared_ptr<Session> &session) {
    const auto vaultId = payload.at("id").get<unsigned int>();

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::Remove,
        .vault_id = vaultId
    }))
        throw std::runtime_error("User does not have permission to remove vault.");

    const auto vault = runtime::Deps::get().storageManager->getVault(vaultId);
    if (!vault) throw std::runtime_error("Vault not found with ID: " + std::to_string(vaultId));

    runtime::Deps::get().storageManager->removeVault(vaultId);
    return {};
}

json Vaults::get(const json &payload, const std::shared_ptr<Session> &session) {
    const auto vaultId = payload.at("id").get<unsigned int>();

    if (!resolver::Admin::has<permission::admin::VaultPermissions>({
        .user = session->user,
        .permission = permission::admin::VaultPermissions::View,
        .vault_id = vaultId
    }))
        throw std::runtime_error("User does not have permission to view vault.");

    const auto vault = runtime::Deps::get().storageManager->getVault(vaultId);
    if (!vault) throw std::runtime_error("Vault not found with ID: " + std::to_string(vaultId));

    json data = {};

    if (vault->type == VaultType::S3) {
        const auto s3Vault = std::static_pointer_cast<S3Vault>(vault);
        data["vault"] = *s3Vault;
        attachRemotePolicyJson(data["vault"], vaultId);
    } else data["vault"] = *vault;

    if (vault->owner_id == session->user->id) data["vault"]["owner"] = session->user->name;
    else data["vault"]["owner"] = db::query::vault::Vault::getVaultOwnersName(vaultId);

    return data;
}

json Vaults::list(const std::shared_ptr<Session> &session) {
    const auto &adminVPerms = session->user->vaultsPerms();
    if (adminVPerms.self.canView() && !(adminVPerms.admin.canView() || adminVPerms.user.canView()))
        return json{{"vaults", db::query::vault::Vault::listUserVaults(session->user->id)}};

    auto vaults = db::query::vault::Vault::listVaults();
    std::erase_if(vaults, [&](const auto &v) {
            return !resolver::Admin::has<permission::admin::VaultPermissions>({
                .user = session->user,
                .permission = permission::admin::VaultPermissions::View,
                .vault_id = v->id
            });
        });

    return json{{"vaults", vaults}};
}

json Vaults::sync(const json &payload, const std::shared_ptr<Session> &session) {
    const auto vaultId = payload.at("id").get<unsigned int>();

    if (!resolver::Vault::has<permission::vault::sync::SyncActionPermissions>({
        .user = session->user,
        .permission = permission::vault::sync::SyncActionPermissions::Trigger,
        .vault_id = vaultId
    })) throw std::runtime_error("User does not have permission to trigger vault.");

    runtime::Deps::get().syncController->runNow(vaultId);
    return {};
}
