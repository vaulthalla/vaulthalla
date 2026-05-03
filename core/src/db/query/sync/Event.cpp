#include "db/query/sync/Event.hpp"
#include "db/Transactions.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/Throughput.hpp"
#include "sync/model/Conflict.hpp"
#include "sync/model/ConflictArtifact.hpp"
#include "sync/model/Artifact.hpp"
#include "fs/model/File.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/encoding/u8.hpp"
#include "config/Registry.hpp"

namespace vh::db::query::sync {

using vh::db::encoding::timestampToString;
using vh::db::encoding::to_utf8_string;

static void build_event(pqxx::work& txn, const Event::EventPtr& event) {
    if (!event) return;

    {
        const auto res = txn.exec(pqxx::prepped{"sync_throughput.read_all_for_run"},
            pqxx::params{event->vault_id, event->run_uuid});
        if (res.empty()) return;
        for (const auto& row : res) event->throughputs.push_back(std::make_unique<vh::sync::model::Throughput>(row));
    }

    {
        const auto& res = txn.exec(pqxx::prepped{"sync_conflict.select_by_event"}, event->id);
        if (res.empty()) return;
        for (const auto& row : res) {
            const auto conflictId = row["id"].as<unsigned int>();
            const auto artifactRes = txn.exec(pqxx::prepped{"sync_conflict_artifact.select_by_conflict"}, conflictId);
            const auto reasonRes = txn.exec(pqxx::prepped{"sync_conflict_reason.select_by_conflict"}, conflictId);
            event->conflicts.push_back(std::make_shared<vh::sync::model::Conflict>(row, artifactRes, reasonRes));
        }
    }
}

void Event::create(const EventPtr& event) {
    const pqxx::params p {
        event->vault_id,
        timestampToString(event->timestamp_begin),
        std::string(vh::sync::model::Event::toString(event->status)),
        std::string(vh::sync::model::Event::toString(event->trigger)),
        event->retry_attempt,
        event->config_hash
    };

    Transactions::exec("SyncQueries::createSyncEvent", [&](pqxx::work& txn) {
        const auto res = txn.exec(pqxx::prepped{"sync_event.create"}, p);
        if (res.empty()) throw std::runtime_error("Failed to create sync event");
        const auto row = res.one_row();
        event->id = row["id"].as<unsigned int>();
        event->run_uuid = row["run_uuid"].as<std::string>();
    });
}

void Event::upsert(const EventPtr& event) {
    Transactions::exec("SyncQueries::upsertSyncEvent", [&](pqxx::work& txn) {
        if (const auto res = txn.exec(pqxx::prepped{"sync_event.upsert"}, event->getParams());
            res.empty()) throw std::runtime_error("Failed to upsert sync event");

        for (const auto& throughput : event->throughputs) {
            const pqxx::params p {
                event->vault_id,
                event->run_uuid,
                throughput->metricToString(),
                throughput->num_ops,
                throughput->size_bytes,
                throughput->duration_ms
            };
            if (const auto res = txn.exec(pqxx::prepped{"sync_throughput.upsert"}, p);
                res.empty()) throw std::runtime_error("Failed to upsert sync throughput");
        }

        for (const auto& conflict : event->conflicts) {
            {
                const pqxx::params p {
                    conflict->event_id,
                    conflict->file_id,
                    conflict->typeToString(),
                    conflict->resolutionToString()
                };

                const auto res = txn.exec(pqxx::prepped{"sync_conflict.upsert"}, p);
                if (res.empty()) throw std::runtime_error("Failed to upsert sync conflict");
                conflict->id = res.one_row()["id"].as<unsigned int>();
            }

            const auto upsertArtifact = [&txn, &conflict](const vh::sync::model::Artifact& artifact) {
                const auto& f = artifact.file;
                const pqxx::params p {
                    conflict->id,
                    artifact.sideToString(),
                    f->size_bytes,
                    f->mime_type,
                    f->content_hash,
                    timestampToString(f->updated_at),
                    f->encryption_iv,
                    f->encrypted_with_key_version,
                    to_utf8_string(f->backing_path.u8string())
                };
                if (const auto res = txn.exec(pqxx::prepped{"sync_conflict_artifact.upsert"}, p);
                    res.empty()) throw std::runtime_error("Failed to upsert sync conflict artifact");
            };

            upsertArtifact(conflict->artifacts.local);
            upsertArtifact(conflict->artifacts.upstream);

            for (auto& reason : conflict->reasons) {
                const pqxx::params p {
                    conflict->id,
                    reason.code,
                    reason.message
                };

                const auto res = txn.exec(pqxx::prepped{"sync_conflict_reason.upsert"}, p);
                if (res.empty()) throw std::runtime_error("Failed to upsert sync conflict reason");
                const auto row = res.one_row();
                reason.id = row["id"].as<unsigned int>();
                reason.conflict_id = conflict->id;
            }
        }
    });
}

std::vector<Event::EventPtr> Event::getEvents(unsigned int vaultId, unsigned int limit, unsigned int offset) {
    return Transactions::exec("SyncQueries::getSyncEvents", [&](pqxx::work& txn) {
        const pqxx::params p { vaultId, limit, offset };
        const auto res = txn.exec(pqxx::prepped{"sync_event.list_for_vault"}, p);
        const auto events = vh::sync::model::sync_events_from_pqxx_res(res);
        for (const auto& event : events) build_event(txn, event);
        return events;
    });
}

std::vector<Event::EventPtr> Event::getEvents(const unsigned int vaultId) {
    constexpr unsigned int limit = 100; // Arbitrary limit to prevent fetching too many events at once
    constexpr unsigned int offset = 0; // For pagination, can be adjusted as needed
    return getEvents(vaultId, limit, offset);
}

Event::EventPtr Event::getLatest(unsigned int vaultId) {
    constexpr unsigned int limit = 1;
    constexpr unsigned int offset = 0;

    return Transactions::exec("SyncQueries::getLatestSyncEvent", [&](pqxx::work& txn) -> EventPtr {
        const pqxx::params p { vaultId, limit, offset };
        const auto res = txn.exec(pqxx::prepped{"sync_event.list_for_vault"}, p);
        if (res.empty()) return nullptr;

        const auto event = std::make_shared<E>(res.one_row());
        build_event(txn, event);
        return event;
    });
}

void Event::heartbeat(const EventPtr& event) {
    Transactions::exec("SyncQueries::heartbeat", [&](pqxx::work& txn) {
        const pqxx::params p { event->vault_id, event->run_uuid, timestampToString(event->heartbeat_at) };
        if (const auto res = txn.exec(pqxx::prepped{"sync_event.touch_heartbeat"}, p);
            res.empty()) throw std::runtime_error("Failed to heartbeat sync event");
    });
}

void Event::purgeOld() {
    const auto& syncConfig = config::Registry::get().sync;

    const auto retention_days = syncConfig.event_audit_retention_days;
    const auto max_entries    = syncConfig.event_audit_max_entries;

    // TODO: expose these in config later, but hardcode for now.
    constexpr int batch_size  = 5000;
    constexpr int max_batches = 200;

    Transactions::exec("Event::purgeOldEvents", [&](pqxx::work& txn) {
        const pqxx::params p{
            retention_days,
            max_entries,
            batch_size,
            max_batches
        };

        txn.exec(pqxx::prepped{"sync_event.cleanup"}, p);
    });
}

}
