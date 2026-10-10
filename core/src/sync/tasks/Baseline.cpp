#include "sync/tasks/Baseline.hpp"

#include "db/query/sync/Conflict.hpp"
#include "log/Registry.hpp"
#include "storage/CloudEngine.hpp"
#include "vault/model/Vault.hpp"

namespace vh::sync::tasks {

void recordBaseline(const std::shared_ptr<storage::CloudEngine>& engine, const model::Baseline& baseline) noexcept {
    if (!engine || !engine->vault || !baseline.file_id) return;
    try {
        db::query::sync::Conflict::upsertBaseline(engine->vault->id, baseline);
    } catch (const std::exception& e) {
        log::Registry::sync()->warn("[SyncTask] Could not record the sync baseline of file {} in vault {}: {}",
                                    baseline.file_id, engine->vault->id, e.what());
    }
}

}
