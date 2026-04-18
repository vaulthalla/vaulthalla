#include "sync/Cloud.hpp"

#include "storage/CloudEngine.hpp"
#include "sync/tasks/Download.hpp"
#include "sync/tasks/Upload.hpp"
#include "sync/tasks/Delete.hpp"
#include "sync/Executor.hpp"

#include "vault/model/Vault.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/fs/Directory.hpp"
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

    const Stage stages[] = {
        {"shared",   [this]{ processSharedOps(); }},
        {"initBins", [this]{ initBins(); }},
        {"sync",     [this]{ sync(); }},
        {"clearBins",[this]{ clearBins(); }},
    };

    runStages(stages);
    shutdown();
}

// ##########################################
// ############# Sync Operations ############
// ##########################################

void Cloud::sync() {
    const auto self = std::static_pointer_cast<Cloud>(shared_from_this());
    Executor::run(self, Planner::build(self, cloudEngine()->remote_policy()));
}

void Cloud::initBins() {
    s3Map = cloudEngine()->getGroupedFilesFromS3();
    s3Files = uMap2Vector(s3Map);

    localFiles = db::query::fs::File::listFilesInDir(engine->vault->id);
    localMap = groupEntriesByPath(localFiles);

    event->heartbeat();

    for (const auto& [path, entry] : intersect(s3Map, localMap))
        remoteHashMap.insert({entry->path.u8string(),
                              cloudEngine()->getRemoteContentHash(entry->path)});
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
