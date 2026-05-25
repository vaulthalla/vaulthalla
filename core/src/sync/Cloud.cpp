#include "sync/Cloud.hpp"

#include "storage/CloudEngine.hpp"
#include "storage/ScopedS3RequestBudget.hpp"
#include "sync/tasks/Download.hpp"
#include "sync/tasks/Upload.hpp"
#include "sync/tasks/Delete.hpp"
#include "sync/Executor.hpp"

#include "vault/model/Vault.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/fs/Directory.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "fs/model/Entry.hpp"
#include "fs/model/File.hpp"
#include "fs/model/Directory.hpp"
#include "fs/model/Path.hpp"

#include "log/Registry.hpp"

#include "sync/model/Event.hpp"
#include "sync/model/Throughput.hpp"
#include "sync/model/Artifact.hpp"
#include "sync/model/ConflictArtifact.hpp"
#include "sync/model/Conflict.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/helpers.hpp"
#include "sync/Planner.hpp"

#include "crypto/id/Generator.hpp"

#include <utility>

using namespace vh::sync;
using namespace vh::sync::model;
using namespace std::chrono;
using namespace vh::storage;
using namespace vh::fs::model;
using namespace vh::crypto;


// ##########################################
// ########### FSTask Overrides #############
// ##########################################

void Cloud::operator()() {
    startTask();

    std::shared_ptr<CloudEngine> cloud;
    try {
        cloud = cloudEngine();
        const ScopedS3RequestBudget s3Budget(cloud, cloud->remote_policy()->s3_request_budget);

        const Stage stages[] = {
            {"shared",   [this]{ processSharedOps(); }},
            {"initBins", [this]{ initBins(); }},
            {"sync",     [this]{ sync(); }},
            {"clearBins",[this]{ clearBins(); }},
        };

        runStages(stages);

        const auto s3Metrics = s3Budget.metrics();
        event->applyS3RequestMetrics(s3Metrics);
        if (s3Metrics.budget_exceeded) {
            event->status = Event::Status::STALLED;
            event->stall_reason = s3Metrics.budget_exceeded_reason;
        }
        log::Registry::sync()->info(
            "[CloudSync] S3 counters for vault {}: LIST={} HEAD={} GET={} PUT={} COPY={} DELETE={} downloaded_bytes={}",
            engine->vault->id,
            event->s3_list_requests,
            event->s3_head_requests,
            event->s3_get_requests,
            event->s3_put_requests,
            event->s3_copy_requests,
            event->s3_delete_requests,
            event->s3_downloaded_bytes);
    } catch (const vh::storage::s3::RequestBudgetExceeded& e) {
        if (cloud) event->applyS3RequestMetrics(cloud->s3RequestMetrics());
        event->status = Event::Status::STALLED;
        event->stall_reason = e.what();
    } catch (const std::exception& e) {
        if (cloud) event->applyS3RequestMetrics(cloud->s3RequestMetrics());
        handleError(std::format("[CloudSync] {}", e.what()));
    } catch (...) {
        if (cloud) event->applyS3RequestMetrics(cloud->s3RequestMetrics());
        handleError("[CloudSync] Unknown exception");
    }

    shutdown();
}

// ##########################################
// ############# Sync Operations ############
// ##########################################

void Cloud::sync() {
    const auto self = std::static_pointer_cast<Cloud>(shared_from_this());
    S3CostEstimate planningNotes;
    const auto plan = Planner::build(self, cloudEngine()->remote_policy(), &planningNotes);
    auto estimate = Planner::estimateS3Cost(plan);
    estimate.archive_tier_downloads_skipped = planningNotes.archive_tier_downloads_skipped;
    event->applyS3CostEstimate(estimate);
    log::Registry::sync()->info(
        "[CloudSync] Estimated S3 cost pressure for vault {}: LIST={} HEAD={} GET={} PUT={} COPY={} DELETE={} body_download_bytes={} upload_bytes={} index_only_objects={} archive_skipped={}",
        engine->vault->id,
        estimate.list_requests,
        estimate.head_requests,
        estimate.get_requests,
        estimate.put_requests,
        estimate.copy_requests,
        estimate.delete_requests,
        estimate.planned_body_download_bytes,
        estimate.planned_upload_bytes,
        estimate.remote_index_objects,
        estimate.archive_tier_downloads_skipped);
    Executor::run(self, plan);
    event->computeDashboardStats();
    if (event->num_failed_ops == 0)
        cloudEngine()->applyRemoteIndexMutation(plan);
}

void Cloud::initBins() {
    const auto cloud = cloudEngine();
    const auto policy = cloud->remote_policy();
    const bool manifestRefreshed = cloud->refreshRemoteIndexFromManifestIfChanged();
    const auto indexSummary = db::query::sync::RemoteObjectIndex::summaryForVault(engine->vault->id);

    if (manifestRefreshed || indexSummary.object_count > 0) {
        if (!manifestRefreshed && indexSummary.isStale(policy->max_remote_index_age)) {
            const auto age = indexSummary.indexed_at
                ? std::to_string(std::time(nullptr) - *indexSummary.indexed_at) + "s old"
                : std::string("missing indexed_at");
            throw SyncStalled(
                "remote index is stale and manifest refresh failed; index is " + age);
        }
        s3Map = groupEntriesByPath(db::query::sync::RemoteObjectIndex::listFilesForVault(engine->vault->id));
    } else {
        s3Map = cloud->getGroupedFilesFromS3();
        db::query::sync::RemoteObjectIndex::replaceFromListObjects(engine->vault->id, uMap2Vector(s3Map));
        cloud->publishRemoteIndexManifest();
    }
    s3Files = uMap2Vector(s3Map);

    localFiles = db::query::fs::File::listFilesInDir(engine->vault->id);
    localMap = groupEntriesByPath(localFiles);

    event->heartbeat();
}

void Cloud::clearBins() {
    localFiles.clear();
    s3Files.clear();
    s3Map.clear();
    localMap.clear();
    remoteHashMap.clear();
}

// ##########################################
// ########### File Operations #############
// ##########################################

void Cloud::upload(const std::shared_ptr<File>& file) {
    push(std::make_shared<tasks::Upload>(
        cloudEngine(), file, op(Throughput::Metric::UPLOAD)));
}

void Cloud::download(const std::shared_ptr<File>& file, const bool freeAfterDownload) {
    push(std::make_shared<tasks::Download>(
        cloudEngine(),
        file,
        event->getOrCreateThroughput(Throughput::Metric::DOWNLOAD).newOp(),
        freeAfterDownload));
}

void Cloud::indexRemoteOnly(const std::shared_ptr<File>& file) {
    push(std::make_shared<tasks::Download>(
        cloudEngine(),
        file,
        event->getOrCreateThroughput(Throughput::Metric::INDEX).newOp(),
        true));
}

void Cloud::remove(const std::shared_ptr<File>& file, const tasks::Delete::Type& type) {
    push(std::make_shared<tasks::Delete>(
        cloudEngine(), file, op(Throughput::Metric::DELETE), type));
}

// ##########################################
// ############ Internal Helpers ############
// ##########################################

std::shared_ptr<CloudEngine> Cloud::cloudEngine() const {
    return std::static_pointer_cast<CloudEngine>(engine);
}

std::vector<EntryKey> Cloud::allKeysSorted() const {
    std::vector<EntryKey> keys;
    keys.reserve(localMap.size() + s3Map.size());

    for (const auto& [k, _] : localMap) keys.push_back({k});
    for (const auto& [k, _] : s3Map)    keys.push_back({k});

    std::ranges::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

void Cloud::ensureDirectoriesFromRemote() {
    for (const auto& dir : cloudEngine()->extractDirectories(uMap2Vector(s3Map))) {
        if (!db::query::fs::Directory::directoryExists(engine->vault->id, dir->path)) {
            dir->parent_id = db::query::fs::Directory::getDirectoryIdByPath(
                engine->vault->id, dir->path.parent_path());

            if (dir->fuse_path.empty())
                dir->fuse_path = engine->paths->absPath(dir->path, PathType::VAULT_ROOT);

            dir->base32_alias = id::Generator({ .namespace_token = dir->name }).generate();
            db::query::fs::Directory::upsertDirectory(dir);
        }
    }
}

// ##########################################
// ########### Conflict Handling ############
// ##########################################

bool Cloud::hasPotentialConflict(const std::shared_ptr<File>& local,
                                   const std::shared_ptr<File>& upstream,
                                   bool upstream_decryption_failure) {
    if (upstream_decryption_failure) return true;
    if (local->size_bytes != upstream->size_bytes) return true;

    if (local->content_hash && upstream->content_hash &&
        *local->content_hash != *upstream->content_hash)
        return true;

    return false;
}

std::shared_ptr<Conflict> Cloud::maybeBuildConflict(
    const std::shared_ptr<File>& local,
    const std::shared_ptr<File>& upstream) const
{
    if (!hasPotentialConflict(local, upstream, false)) return nullptr;

    auto c = std::make_shared<Conflict>();
    c->failed_to_decrypt_upstream = false; // Planner-time default
    c->artifacts.local = Artifact(local, Artifact::Side::LOCAL);
    c->artifacts.upstream = Artifact(upstream, Artifact::Side::UPSTREAM);
    c->analyze();

    if (c->reasons.empty()) {
        log::Registry::sync()->debug(
            "[SyncTask] hasPotentialConflict() returned true, but no reasons were identified. Possible detection bug.");
        return nullptr;
    }

    c->created_at = system_clock::to_time_t(system_clock::now());
    c->file_id = local->id;
    c->event_id = event->id;

    return c;
}

bool Cloud::handleConflict(const std::shared_ptr<Conflict>& c) const {
    const bool ok = engine->sync->resolve_conflict(c);
    if (ok) c->resolved_at = system_clock::to_time_t(system_clock::now());
    event->conflicts.push_back(c);
    return !ok; // true => unresolved (Ask)
}

// ##########################################
// ########### Static Helpers ###############
// ##########################################

uintmax_t Cloud::computeReqFreeSpaceForDownload(
    const std::vector<std::shared_ptr<File> >& files) {
    uintmax_t totalSize = 0;
    for (const auto& file : files) totalSize += file->size_bytes;
    return totalSize;
}

std::vector<std::shared_ptr<File>> Cloud::uMap2Vector(
    std::unordered_map<std::u8string, std::shared_ptr<File>>& map)
{
    std::vector<std::shared_ptr<File>> files;
    files.reserve(map.size());

    std::ranges::transform(
        map.begin(), map.end(),
        std::back_inserter(files),
        [](const auto& pair) { return pair.second; });

    return files;
}

std::unordered_map<std::u8string, std::shared_ptr<File>> Cloud::intersect(
    const std::unordered_map<std::u8string, std::shared_ptr<File> >& a,
    const std::unordered_map<std::u8string, std::shared_ptr<File> >& b)
{
    std::unordered_map<std::u8string, std::shared_ptr<File>> result;
    for (const auto& item : a)
        if (b.contains(item.first))
            result.insert(item);

    return result;
}
