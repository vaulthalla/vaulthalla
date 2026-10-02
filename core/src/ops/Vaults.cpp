#include "ops/Vaults.hpp"

#include "db/query/identities/User.hpp"
#include "db/query/sync/Policy.hpp"
#include "db/query/vault/APIKey.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/vault/Waiver.hpp"
#include "identities/User.hpp"
#include "rbac/resolver/admin/all.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "runtime/Deps.hpp"
#include "storage/Engine.hpp"
#include "storage/Manager.hpp"
#include "storage/s3/Controller.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "sync/Controller.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/Waiver.hpp"
#include "vault/APIKeyManager.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/terms/waiver.hpp"

#include <paths.h>

#include <algorithm>
#include <mutex>
#include <utility>

namespace vh::ops::vaults {

namespace {

using vault::model::S3Vault;
using vault::model::VaultType;
using VaultPerm = rbac::permission::admin::VaultPermissions;
using KeyPerm = rbac::permission::admin::keys::APIPermissions;
using SyncConfigPerm = rbac::permission::vault::sync::SyncConfigPermissions;
using SyncActionPerm = rbac::permission::vault::sync::SyncActionPermissions;

bool canOnVault(const Actor& actor, const VaultPerm permission, const unsigned int vaultId) {
    return rbac::resolver::Admin::has<VaultPerm>({.user = actor, .permission = permission, .vault_id = vaultId});
}

bool canCreateFor(const Actor& actor, const unsigned int ownerId) {
    return rbac::resolver::Admin::has<VaultPerm>({.user = actor, .permission = VaultPerm::Create, .target_user_id = ownerId});
}

void requireConsume(const Actor& actor, const unsigned int keyId) {
    if (!db::query::vault::APIKey::getAPIKey(keyId)) throw NotFound("API key not found: " + std::to_string(keyId));
    if (!rbac::resolver::Admin::has<KeyPerm>({.user = actor, .permission = KeyPerm::Consume, .api_key_id = keyId}))
        throw Denied("you do not have permission to use api-key " + std::to_string(keyId) + " for this vault");
}

VaultPtr requireVault(const unsigned int vaultId) {
    if (vaultId == 0) throw Invalid("vault ID must be a positive integer");
    auto vault = runtime::Deps::get().storageManager->getVault(vaultId);
    if (!vault) throw NotFound("vault not found: " + std::to_string(vaultId));
    return vault;
}

// Copies the vault so a patch never mutates the engine's live object before it is persisted.
VaultPtr copyOf(const VaultPtr& vault) {
    if (vault->type == VaultType::S3) return std::make_shared<S3Vault>(*std::static_pointer_cast<S3Vault>(vault));
    return std::make_shared<vault::model::Vault>(*vault);
}

std::optional<std::string> normalizedTier(const unsigned int keyId, const std::optional<std::string>& requested) {
    const auto key = db::query::vault::APIKey::getAPIKey(keyId);
    if (!key) throw NotFound("API key not found: " + std::to_string(keyId));
    const auto tier = storage::s3::provider::resolve(key->provider)->normalizeStorageTier(requested);
    if (!tier.ok) throw Invalid(tier.error);
    return tier.normalized_id;
}

void applySync(sync::model::Policy& policy, const VaultType type, const SyncPatch& patch) {
    try {
        if (patch.interval) policy.interval = *patch.interval;
        if (patch.enabled) policy.enabled = *patch.enabled;
        if (type == VaultType::Local) {
            if (patch.strategy || patch.max_remote_index_age || patch.s3_budget_preset || !patch.s3_budget.empty())
                throw Invalid("sync strategy, remote index age and S3 request budgets only apply to S3 vaults");
            if (patch.conflict_policy)
                dynamic_cast<sync::model::LocalPolicy&>(policy).conflict_policy =
                    sync::model::fsConflictPolicyFromString(*patch.conflict_policy);
        } else {
            auto& remote = dynamic_cast<sync::model::RemotePolicy&>(policy);
            if (patch.strategy) remote.strategy = sync::model::strategyFromString(*patch.strategy);
            if (patch.conflict_policy)
                remote.conflict_policy = sync::model::rsConflictPolicyFromString(*patch.conflict_policy);
            if (patch.max_remote_index_age) remote.max_remote_index_age = *patch.max_remote_index_age;
            if (patch.s3_budget_preset)
                remote.s3_request_budget = sync::model::s3RequestBudgetForPreset(
                    sync::model::s3BudgetPresetFromString(*patch.s3_budget_preset));
            auto& b = remote.s3_request_budget;
            const auto& p = patch.s3_budget;
            if (p.list) b.max_list_requests = *p.list;
            if (p.head) b.max_head_requests = *p.head;
            if (p.get) b.max_get_requests = *p.get;
            if (p.put) b.max_put_requests = *p.put;
            if (p.copy) b.max_copy_requests = *p.copy;
            if (p.del) b.max_delete_requests = *p.del;
            if (p.downloaded_bytes) b.max_downloaded_bytes = *p.downloaded_bytes;
        }
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        throw Invalid(e.what());
    }
    policy.interval = sync::model::Policy::clampInterval(policy.interval);
    policy.rehash_config();
}

PolicyPtr freshPolicy(const VaultType type) {
    if (type == VaultType::S3) return std::make_shared<sync::model::RemotePolicy>();
    return std::make_shared<sync::model::LocalPolicy>();
}

PolicyPtr copyPolicy(const PolicyPtr& policy, const VaultType type) {
    if (type == VaultType::S3)
        return std::make_shared<sync::model::RemotePolicy>(dynamic_cast<const sync::model::RemotePolicy&>(*policy));
    return std::make_shared<sync::model::LocalPolicy>(dynamic_cast<const sync::model::LocalPolicy&>(*policy));
}

// True when the bucket already holds objects, so encrypting (or not) over it changes what existing readers see.
bool bucketHoldsData(const S3Vault& s3) {
    if (paths::testMode) return false;
    const auto key = runtime::Deps::get().apiKeyManager->getAPIKey(s3.api_key_id);
    const storage::s3::Controller controller(key, s3.bucket);
    if (const auto [valid, message] = controller.validateAPICredentials(); !valid)
        throw Invalid("cannot reach the S3 bucket with this API key: " + message);
    return !controller.isBucketEmpty();
}

std::string waiverTextFor(const S3Vault& s3) {
    return std::string(s3.encrypt_upstream ? vault::terms::ENABLE_UPSTREAM_ENCRYPTION_WAIVER
                                           : vault::terms::DISABLE_UPSTREAM_ENCRYPTION_WAIVER);
}

void recordWaiver(const Actor& actor, const std::shared_ptr<S3Vault>& s3) {
    auto waiver = std::make_shared<sync::model::Waiver>();
    waiver->vault = s3;
    waiver->user = actor;
    waiver->apiKey = db::query::vault::APIKey::getAPIKey(s3->api_key_id);
    waiver->encrypt_upstream = s3->encrypt_upstream;
    waiver->waiver_text = waiverTextFor(*s3);
    db::query::vault::Waiver::addWaiver(waiver);
}

void pushSyncToEngine(const unsigned int vaultId, const PolicyPtr& policy) {
    if (const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId)) {
        std::unique_lock lock(engine->mutex);
        engine->sync = policy;
    }
    if (runtime::Deps::get().syncController) runtime::Deps::get().syncController->refreshEngines();
}

PolicyPtr currentPolicy(const unsigned int vaultId) {
    if (const auto engine = runtime::Deps::get().storageManager->getEngine(vaultId)) {
        std::shared_lock lock(engine->mutex);
        if (engine->sync) return engine->sync;
    }
    auto policy = db::query::sync::Policy::getSync(vaultId);
    if (!policy) throw NotFound("sync policy not found for vault " + std::to_string(vaultId));
    return policy;
}

}

VaultPtr create(const Actor& actor, const Create& req) {
    requireActor(actor);
    const auto ownerId = req.owner_id.value_or(actor->id);
    if (!db::query::identities::User::getUserById(ownerId)) throw NotFound("owner not found: " + std::to_string(ownerId));
    if (!canCreateFor(actor, ownerId)) throw Denied("you do not have permission to create vaults for this owner");
    if (req.name.empty()) throw Invalid("vault name is required");
    if (db::query::vault::Vault::vaultExists(req.name, ownerId))
        throw Conflict("a vault named '" + req.name + "' already exists for this owner");

    VaultPtr vault;
    if (req.type == VaultType::S3) {
        if (!req.s3) throw Invalid("S3 vaults need an API key and a bucket");
        if (req.s3->bucket.empty()) throw Invalid("bucket is required for S3 vaults");
        requireConsume(actor, req.s3->api_key_id);
        auto s3 = std::make_shared<S3Vault>(req.name, req.s3->api_key_id, req.s3->bucket);
        s3->storage_tier_id = normalizedTier(req.s3->api_key_id, req.s3->storage_tier);
        s3->encrypt_upstream = req.s3->encrypt_upstream.value_or(true);
        vault = s3;
    } else {
        if (req.s3) throw Invalid("API key and bucket only apply to S3 vaults");
        vault = std::make_shared<vault::model::Vault>();
    }
    vault->type = req.type;
    vault->name = req.name;
    vault->owner_id = ownerId;
    vault->description = req.description;
    vault->quota = req.quota;
    vault->slug = req.slug;
    vault->fuse_name = req.fuse_name;

    auto policy = freshPolicy(req.type);
    applySync(*policy, req.type, req.sync);

    std::shared_ptr<S3Vault> needsWaiver;
    if (req.type == VaultType::S3) {
        const auto s3 = std::static_pointer_cast<S3Vault>(vault);
        if (bucketHoldsData(*s3)) {
            if (!req.accept_waiver) throw NeedsConfirmation("encryption_waiver", waiverTextFor(*s3));
            needsWaiver = s3;
        }
    }

    vault = runtime::Deps::get().storageManager->addVault(vault, policy);
    if (needsWaiver) {
        try {
            recordWaiver(actor, std::static_pointer_cast<S3Vault>(vault));
        } catch (...) {
            runtime::Deps::get().storageManager->removeVault(vault->id);
            throw;
        }
    }
    return vault;
}

VaultPtr update(const Actor& actor, const Update& req) {
    requireActor(actor);
    const auto existing = requireVault(req.id);
    if (!canOnVault(actor, VaultPerm::Edit, existing->id)) throw Denied("you do not have permission to edit this vault");

    auto staged = copyOf(existing);
    if (req.name) {
        if (req.name->empty()) throw Invalid("vault name cannot be empty");
        if (*req.name != existing->name && db::query::vault::Vault::vaultExists(*req.name, req.owner_id.value_or(existing->owner_id)))
            throw Conflict("a vault named '" + *req.name + "' already exists for this owner");
        staged->name = *req.name;
    }
    if (req.description) staged->description = *req.description;
    if (req.quota) staged->quota = *req.quota;
    if (req.slug) staged->slug = *req.slug;
    if (req.fuse_name) staged->fuse_name = *req.fuse_name;
    if (req.is_active) staged->is_active = *req.is_active;
    if (req.owner_id && *req.owner_id != existing->owner_id) {
        if (!db::query::identities::User::getUserById(*req.owner_id))
            throw NotFound("owner not found: " + std::to_string(*req.owner_id));
        // Handing a vault to someone is creating one for them.
        if (!canCreateFor(actor, *req.owner_id)) throw Denied("you do not have permission to give vaults to that owner");
        staged->owner_id = *req.owner_id;
    }

    bool encryptionChanged = false;
    if (existing->type == VaultType::S3) {
        const auto s3 = std::static_pointer_cast<S3Vault>(staged);
        const auto before = std::static_pointer_cast<S3Vault>(existing);
        if (req.api_key_id && *req.api_key_id != before->api_key_id) {
            requireConsume(actor, *req.api_key_id);
            s3->api_key_id = *req.api_key_id;
        }
        if (req.bucket) {
            if (req.bucket->empty()) throw Invalid("bucket cannot be empty");
            s3->bucket = *req.bucket;
        }
        // The tier is provider-specific: re-normalize whenever it or the key (provider) changes.
        if (req.storage_tier || s3->api_key_id != before->api_key_id)
            s3->storage_tier_id = normalizedTier(s3->api_key_id, req.storage_tier ? *req.storage_tier : s3->storage_tier_id);
        if (req.encrypt_upstream && *req.encrypt_upstream != before->encrypt_upstream) {
            s3->encrypt_upstream = *req.encrypt_upstream;
            encryptionChanged = true;
        }
    } else if (req.api_key_id || req.bucket || req.storage_tier || req.encrypt_upstream) {
        throw Invalid("API key, bucket, storage tier and encryption only apply to S3 vaults");
    }

    if (encryptionChanged) {
        if (!rbac::resolver::Vault::has<SyncActionPerm>({.user = actor, .permission = SyncActionPerm::SignWaiver,
                                                        .vault_id = existing->id}))
            throw Denied("changing upstream encryption requires the vault's sign-waiver permission");
        const auto s3 = std::static_pointer_cast<S3Vault>(staged);
        if (bucketHoldsData(*s3) && !req.accept_waiver) throw NeedsConfirmation("encryption_waiver", waiverTextFor(*s3));
    }

    PolicyPtr newPolicy;
    if (!req.sync.empty()) {
        if (!rbac::resolver::Vault::has<SyncConfigPerm>({.user = actor, .permission = SyncConfigPerm::Edit,
                                                        .vault_id = existing->id}))
            throw Denied("you do not have permission to change this vault's sync settings");
        newPolicy = copyPolicy(currentPolicy(existing->id), existing->type);
        applySync(*newPolicy, existing->type, req.sync);
    }

    if (newPolicy) db::query::vault::Vault::updateVaultSync(newPolicy, existing->type);
    runtime::Deps::get().storageManager->updateVault(staged);
    if (encryptionChanged) recordWaiver(actor, std::static_pointer_cast<S3Vault>(staged));
    if (newPolicy) pushSyncToEngine(existing->id, currentPolicy(existing->id));
    return requireVault(existing->id);
}

VaultPtr remove(const Actor& actor, const unsigned int vaultId) {
    requireActor(actor);
    if (!canOnVault(actor, VaultPerm::Remove, vaultId)) throw Denied("you do not have permission to remove this vault");
    auto vault = requireVault(vaultId);
    runtime::Deps::get().storageManager->removeVault(vaultId);
    return vault;
}

Details get(const Actor& actor, const unsigned int vaultId) {
    requireActor(actor);
    if (!canOnVault(actor, VaultPerm::View, vaultId)) throw Denied("you do not have permission to view this vault");
    Details out{.vault = requireVault(vaultId), .sync = nullptr, .owner_name = {}};
    out.sync = currentPolicy(vaultId);
    if (const auto owner = db::query::identities::User::getUserById(out.vault->owner_id)) out.owner_name = owner->name;
    return out;
}

std::vector<VaultPtr> list(const Actor& actor, db::model::ListQueryParams params,
                           const std::optional<vault::model::VaultType> type) {
    requireActor(actor);
    const auto& perms = actor->vaultsPerms();
    if (perms.self.canView() && !(perms.admin.canView() || perms.user.canView()))
        return db::query::vault::Vault::listUserVaults(actor->id, type, std::move(params));

    auto vaults = db::query::vault::Vault::listVaults(type, std::move(params));
    std::erase_if(vaults, [&](const VaultPtr& v) { return !v || !canOnVault(actor, VaultPerm::View, v->id); });
    return vaults;
}

PolicyPtr updateSync(const Actor& actor, const unsigned int vaultId, const SyncPatch& patch) {
    requireActor(actor);
    const auto vault = requireVault(vaultId);
    if (!rbac::resolver::Vault::has<SyncConfigPerm>({.user = actor, .permission = SyncConfigPerm::Edit, .vault_id = vaultId}))
        throw Denied("you do not have permission to change this vault's sync settings");
    if (patch.empty()) throw Invalid("no sync settings to change");

    auto policy = copyPolicy(currentPolicy(vaultId), vault->type);
    applySync(*policy, vault->type, patch);
    db::query::vault::Vault::updateVaultSync(policy, vault->type);
    pushSyncToEngine(vaultId, policy);
    return policy;
}

SyncStart triggerSync(const Actor& actor, const unsigned int vaultId) {
    requireActor(actor);
    if (!rbac::resolver::Vault::has<SyncActionPerm>({.user = actor, .permission = SyncActionPerm::Trigger, .vault_id = vaultId}))
        throw Denied("you do not have permission to trigger a sync for this vault");
    (void)requireVault(vaultId);
    using RunNowResult = sync::Controller::RunNowResult;
    switch (runtime::Deps::get().syncController->runNow(vaultId)) {
        case RunNowResult::Started: return SyncStart::Started;
        case RunNowResult::Rerun: return SyncStart::RerunQueued;
        case RunNowResult::NoTask:
        default:
            throw NotFound("no sync task is loaded for vault " + std::to_string(vaultId) +
                           "; nothing was started (is its storage engine initialized?)");
    }
}

}
