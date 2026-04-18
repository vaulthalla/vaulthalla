#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "storage/CloudEngine.hpp"
#include "config/Registry.hpp"
#include "vault/model/Vault.hpp"
#include "vault/model/S3Vault.hpp"
#include "identities/User.hpp"
#include "fs/model/Path.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/identities/User.hpp"
#include "log/Registry.hpp"
#include "seed/include/seed_db.hpp"
#include "crypto/id/Generator.hpp"

using namespace vh::storage;
using namespace vh::vault::model;
using namespace vh::identities;
using namespace vh::config;
using namespace vh::fs::model;
using namespace vh::crypto;

Manager::Manager() = default;

void Manager::initStorageEngines() {
    log::Registry::storage()->debug("[StorageManager] Initializing storage engines...");
    std::scoped_lock lock(mutex_);

    if (const auto& config = Registry::get().dev; config.enabled && config.init_r2_test_vault)
        if (const auto admin = db::query::identities::User::getUserByName("admin");
            !db::query::vault::Vault::vaultExists("R2 Test Vault", admin->id)) seed::initDevCloudVault();

    engines_.clear();

    try {
        for (auto& vault : db::query::vault::Vault::listVaults()) {
            log::Registry::storage()->debug("[StorageManager] Initializing StorageEngine for Vault {} (ID: {}, Type: {})",
                                          vault->name, vault->id, to_string(vault->type));
            std::shared_ptr<Engine> engine;
            if (vault->type == VaultType::Local) engine = std::make_shared<Engine>(vault);
            else if (vault->type == VaultType::S3) {
                const auto s3Vault = std::static_pointer_cast<S3Vault>(vault);
                engine = std::make_shared<CloudEngine>(s3Vault);
            }
            engines_[engine->paths->absRelToRoot(engine->paths->vaultRoot, PathType::FUSE_ROOT)] = engine;
            vaultToEngine_[vault->id] = engine;
        }
    } catch (const std::exception& e) {
        log::Registry::storage()->error("[StorageManager] Error initializing storage engines: {}", e.what());
        throw;
    }
}

std::shared_ptr<Engine> Manager::resolveStorageEngine(const fs::path& fusePath) const {
    std::scoped_lock lock(mutex_);

    fs::path current;
    for (const auto& part : fusePath.lexically_normal()) {
        current /= part;

        // Remove trailing slash by converting to generic form and trimming
        std::string currentStr = current.generic_string();
        if (!currentStr.empty() && currentStr.back() == '/') currentStr.pop_back();

        if (engines_.contains(currentStr)) return engines_.at(currentStr);
    }

    log::Registry::storage()->warn("[StorageManager] No storage engine found for path: {}", fusePath.string());
    log::Registry::storage()->info("[StorageManager] Available storage engines:");
    for (const auto& [path, engine] : engines_)
        log::Registry::storage()->info(" - {} (Vault ID: {}, Type: {})", path, engine->vault->id, to_string(engine->vault->type));

    return nullptr;
}

std::vector<std::shared_ptr<Engine>> Manager::getEngines() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::shared_ptr<Engine>> engines;
    engines.reserve(engines_.size());
    for (const auto& [_, engine] : engines_) engines.push_back(engine);
    return engines;
}

void Manager::initUserStorage(const std::shared_ptr<User>& user) {
    try {
        log::Registry::storage()->debug("[StorageManager] Initializing storage user storage...");

        if (!user->id) throw std::runtime_error("User ID is not set. Cannot initialize storage.");

        auto vault = std::make_shared<Vault>();
        vault->name = user->name + "'s Local Disk Vault";
        vault->description = "Default local disk vault for " + user->name;
        vault->mount_point = id::Generator({ .namespace_token = vault->name }).generate();

        {
            std::scoped_lock lock(mutex_);
            vault->id = db::query::vault::Vault::upsertVault(vault);
            vault = db::query::vault::Vault::getVault(vault->id);
        }

        if (!vault) throw std::runtime_error("Failed to create or retrieve vault for user: " + user->name);

        vaultToEngine_[vault->id] = std::make_shared<Engine>(vault);

        log::Registry::storage()->info("[StorageManager] User storage initialized for user: {} (ID: {})",
                                              user->name, user->id);
    } catch (const std::exception& e) {
        log::Registry::storage()->error("[StorageManager] Error initializing user storage: {}", e.what());
        throw;
    }
}

std::shared_ptr<Vault> Manager::addVault(std::shared_ptr<Vault> vault,
                                                const std::shared_ptr<sync::model::Policy>& sync) {
    if (!vault) throw std::invalid_argument("Vault cannot be null");
    std::scoped_lock lock(mutex_);

    vault->mount_point = id::Generator({ .namespace_token = vault->name }).generate();
    vault->id = db::query::vault::Vault::upsertVault(vault, sync);
    vault = db::query::vault::Vault::getVault(vault->id);
    const auto engine = std::make_shared<Engine>(vault);
    engines_[engine->paths->absRelToRoot(engine->paths->vaultRoot, PathType::FUSE_ROOT)] = engine;
    vaultToEngine_[vault->id] = engine;

    log::Registry::storage()->info("[StorageManager] Added new vault with ID: {}, Name: {}, Type: {}",
                                              vault->id, vault->name, to_string(vault->type));

    return vault;
}

void Manager::updateVault(const std::shared_ptr<Vault>& vault) {
    if (!vault) throw std::invalid_argument("Vault cannot be null");
    if (vault->id == 0) throw std::invalid_argument("Vault ID cannot be zero");
    std::scoped_lock lock(mutex_);
    db::query::vault::Vault::upsertVault(vault);
    vaultToEngine_[vault->id]->vault = vault;
    log::Registry::storage()->info("[StorageManager] Updated vault with ID: {}", vault->id);
}

void Manager::removeVault(const unsigned int vaultId) {
    std::scoped_lock lock(mutex_);
    db::query::vault::Vault::removeVault(vaultId);

    vaultToEngine_.erase(vaultId);
    log::Registry::storage()->info("[StorageManager] Removed vault with ID: {}", vaultId);
}

std::shared_ptr<Vault> Manager::getVault(const unsigned int vaultId) const {
    std::scoped_lock lock(mutex_);
    if (vaultToEngine_.contains(vaultId)) return vaultToEngine_.at(vaultId)->vault;
    return db::query::vault::Vault::getVault(vaultId);
}

std::shared_ptr<Engine> Manager::getEngine(const unsigned int id) const {
    std::scoped_lock lock(mutex_);
    if (!vaultToEngine_.contains(id)) {
        log::Registry::storage()->warn("[StorageManager] No engine found for vault ID: {}", id);
        return nullptr;
    }
    return vaultToEngine_.at(id);
}

void Manager::registerOpenHandle(const fuse_ino_t ino) {
    std::scoped_lock lock(openHandleMutex_);
    ++openHandleCounts_[ino];
}

void Manager::closeOpenHandle(const fuse_ino_t ino) {
    std::scoped_lock lock(openHandleMutex_);
    if (--openHandleCounts_[ino] == 0) openHandleCounts_.erase(ino);
}

unsigned int Manager::getOpenHandleCount(const fuse_ino_t ino) const {
    std::scoped_lock lock(openHandleMutex_);
    if (openHandleCounts_.contains(ino)) return openHandleCounts_.at(ino);
    return 0;
}
