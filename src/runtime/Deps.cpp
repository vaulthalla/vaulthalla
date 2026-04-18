#include "runtime/Deps.hpp"

#include "UsageManager.hpp"
#include "auth/Manager.hpp"
#include "crypto/secrets/Manager.hpp"
#include "fs/cache/Registry.hpp"
#include "stats/model/CacheStats.hpp"
#include "storage/Manager.hpp"
#include "sync/Controller.hpp"
#include "vault/APIKeyManager.hpp"

namespace vh::runtime {

    Deps& Deps::get() {
        static Deps instance;
        return instance;
    }

    bool Deps::initialized() const {
        return storageManager
            || apiKeyManager
            || authManager
            || sessionManager
            || secretsManager
            || syncController
            || fsCache
            || shellUsageManager
            || httpCacheStats;
    }

    void Deps::init() {
        auto& deps = get();
        if (deps.initialized()) return;

        deps.storageManager = std::make_shared<storage::Manager>();
        deps.apiKeyManager = std::make_shared<vault::APIKeyManager>();
        deps.authManager = std::make_shared<auth::Manager>();
        deps.sessionManager = std::make_shared<auth::session::Manager>();
        deps.secretsManager = std::make_shared<crypto::secrets::Manager>();
        deps.fsCache = std::make_shared<fs::cache::Registry>();
        deps.shellUsageManager = std::make_shared<protocols::shell::UsageManager>();
        deps.httpCacheStats = std::make_shared<stats::model::CacheStats>();
    }

    void Deps::setSyncController(std::shared_ptr<sync::Controller> controller) {
        get().syncController = std::move(controller);
    }

}
