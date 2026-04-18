#include "vault/model/Stat.hpp"
#include "vault/model/Capacity.hpp"
#include "runtime/Deps.hpp"
#include "storage/Manager.hpp"
#include "storage/Engine.hpp"
#include "sync/model/Event.hpp"

#include <nlohmann/json.hpp>

using namespace vh::vault::model;

Stat::Stat(unsigned int vaultId)
    : vault_id(vaultId),
      capacity(std::make_shared<Capacity>(vaultId)) {}

void vh::vault::model::to_json(nlohmann::json& j, const std::shared_ptr<Stat>& s) {
    j["capacity"] = s->capacity;
    j["latest_sync_event"] = runtime::Deps::get().storageManager->getEngine(s->vault_id)->latestSyncEvent;
}
