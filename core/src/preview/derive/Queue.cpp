#include "preview/derive/Queue.hpp"

#include "config/Registry.hpp"
#include "db/query/fs/File.hpp"
#include "fs/model/File.hpp"
#include "log/Registry.hpp"
#include "preview/Plan.hpp"
#include "preview/derive/Runner.hpp"
#include "storage/Engine.hpp"
#include "storage/PlaintextReader.hpp"
#include "vault/model/Vault.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vh::preview::derive {

// Named (not anonymous): Queue.cpp shares unity chunks with the rest of preview::derive and preview.
namespace queue_impl {

using Clock = std::chrono::steady_clock;

// How long a transient failure (content unavailable, I/O, helper vanished, cancelled) is reported to pollers as
// Failed. It is never persisted: the next request after this window tries again.
constexpr auto kTransientFailureMemo = std::chrono::seconds(30);
constexpr std::string_view kDefaultVariant = "v1";
constexpr std::string_view kMaxTriangles = "2000000";
constexpr std::string_view kPosterMaxWidth = "1280";

// Which source files a kind applies to.
enum class Applies { Step, Media, Video };

struct KindSpec {
    std::string_view kind;
    std::string_view helper;
    std::string_view command;
    uint32_t generatorVersion;   // bump when a helper's output for the same input changes: old artifacts go stale
    Applies applies;
    bool transcode;              // subject to preview.media.transcode
};

// The one table of derived kinds: helper, command and generator version per kind.
constexpr std::array<KindSpec, 6> kKinds{{
    {"model-glb", kCadHelper, "convert-step", 1, Applies::Step, false},
    {"poster-jpg", kMediaHelper, "poster", 1, Applies::Media, false},
    {"probe-json", kMediaHelper, "probe", 1, Applies::Media, false},
    {"transcode-h264-480", kMediaHelper, "transcode", 1, Applies::Video, true},
    {"transcode-h264-720", kMediaHelper, "transcode", 1, Applies::Video, true},
    {"transcode-h264-1080", kMediaHelper, "transcode", 1, Applies::Video, true},
}};

[[nodiscard]] const KindSpec* specFor(const std::string_view kind) {
    const auto it = std::ranges::find(kKinds, kind, &KindSpec::kind);
    return it == kKinds.end() ? nullptr : &*it;
}

[[nodiscard]] bool applicable(const KindSpec& spec, const fs::model::File& file) {
    const auto plan = classify(file);
    switch (spec.applies) {
        case Applies::Step: return plan.derived && *plan.derived == "model-glb";
        case Applies::Media: return plan.renderer == "video" || plan.renderer == "audio";
        case Applies::Video: return plan.renderer == "video";
    }
    return false;
}

[[nodiscard]] std::vector<std::string> argsFor(const KindSpec& spec) {
    if (spec.kind == "model-glb") return {"--max-triangles", std::string(kMaxTriangles)};
    if (spec.kind == "poster-jpg") return {"--max-width", std::string(kPosterMaxWidth)};
    if (spec.transcode) {
        constexpr std::string_view prefix = "transcode-";
        return {"--profile", std::string(spec.kind.substr(prefix.size())),
                "--hwaccel", config::previewHwaccelToString(config::Registry::get().preview.media.hwaccel)};
    }
    return {};
}

// Failures that are a property of this exact source generation and helper version: retrying cannot help, so they
// are negatively cached. Everything else (I/O, a missing helper, cancellation, protocol trouble) is transient.
// Integrity failures are transient too: the usual cause is the file being replaced between the generation check
// and the read (the database row is updated after the bytes), and genuine corruption is already flagged and failed
// fast by crypto::IntegrityRegistry.
[[nodiscard]] bool deterministic(const std::string_view reason) {
    return reason == "invalid_input" || reason == "limit_exceeded" || reason == "unsupported" ||
           reason == "crashed" || reason == "timeout";
}

// The file's content generation as the database records it now (rename/move keep it; every write changes it).
[[nodiscard]] bool sourceStillCurrent(const cache::ArtifactKey& key) {
    const auto current = db::query::fs::File::getFileById(key.file_id);
    return current && storage::generationOf(*current).sourceId() == key.source_id;
}

struct Job {
    std::shared_ptr<storage::Engine> engine;
    std::shared_ptr<fs::model::File> file;
    cache::ArtifactKey key;
    const KindSpec* spec{};
    std::string id;   // key.canonical()
};

enum class Outcome { Completed, Failed, Discarded };

}

struct Queue::Impl {
    mutable std::mutex mutex;
    std::condition_variable_any cv;
    std::deque<std::shared_ptr<queue_impl::Job>> pending;
    std::unordered_set<std::string> inflight;   // pending + running, by canonical key
    std::unordered_map<std::string, std::pair<std::string, queue_impl::Clock::time_point>> transient;
    std::vector<std::jthread> workers;
    bool stopping = false;
    uint64_t running = 0, completed = 0, failed = 0, rejectedBusy = 0;

    void ensureWorkers() {   // under mutex
        if (!workers.empty()) return;
        const auto count = std::max<uint32_t>(1, config::Registry::get().preview.derive.max_concurrency);
        workers.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
            workers.emplace_back([this](const std::stop_token& stop) { workerLoop(stop); });
    }

    void workerLoop(const std::stop_token& stop) {
        while (true) {
            std::shared_ptr<queue_impl::Job> job;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, stop, [this] { return !pending.empty(); });
                if (stop.stop_requested()) return;
                job = std::move(pending.front());
                pending.pop_front();
                ++running;
            }

            auto outcome = queue_impl::Outcome::Discarded;
            std::string transientReason;
            try {
                outcome = execute(*job, stop, transientReason);
            } catch (const std::exception& e) {
                // Never let a job take the worker down.
                transientReason = "internal";
                log::Registry::thumb()->warn("[preview::derive] {} for file {} failed: internal: {}", job->key.kind,
                                             job->key.file_id, e.what());
            }

            const std::scoped_lock lock(mutex);
            --running;
            inflight.erase(job->id);
            if (outcome == queue_impl::Outcome::Completed) ++completed;
            else if (outcome == queue_impl::Outcome::Failed || !transientReason.empty()) ++failed;
            if (!transientReason.empty())
                transient[job->id] = {transientReason, queue_impl::Clock::now() + queue_impl::kTransientFailureMemo};
        }
    }

    static queue_impl::Outcome execute(const queue_impl::Job& job, const std::stop_token& stop,
                                       std::string& transientReason) {
        const auto& key = job.key;
        // File id, kind and reason only: never content (helper messages are the helpers' own diagnostics; stderr is
        // never logged).
        const auto warn = [&key](const std::string_view reason, const std::string& message) {
            const auto detail = message.starts_with(reason) ? message : std::string(reason) + ": " + message;
            if (reason == "cancelled")
                log::Registry::thumb()->info("[preview::derive] {} for file {} (vault {}) cancelled", key.kind,
                                             key.file_id, key.vault_id);
            else
                log::Registry::thumb()->warn("[preview::derive] {} for file {} (vault {}) failed: {}", key.kind,
                                             key.file_id, key.vault_id, detail);
        };

        // A concurrent request may have produced (or failed) this exact key between lookup and enqueue.
        if (cache::Store::lookup(job.engine, key).status != cache::LookupStatus::Missing)
            return queue_impl::Outcome::Discarded;

        // The snapshot names the generation the key was built from; if the file was rewritten since, its bytes on
        // disk belong to another IV.
        if (!queue_impl::sourceStillCurrent(key)) {
            log::Registry::thumb()->debug("[preview::derive] {} for file {}: the source changed before the job ran",
                                          key.kind, key.file_id);
            return queue_impl::Outcome::Discarded;
        }

        const auto limits = limitsFromConfig();
        std::optional<cache::Store::Writer> writer;
        std::string reason, message;
        try {
            const auto reader = job.engine->openPlaintextReader(job.file);
            writer.emplace(cache::Store::begin(job.engine, key, limits.maxOutputBytes));

            RunRequest request;
            request.executable = Runner::helperPath(job.spec->helper);
            request.command = std::string(job.spec->command);
            request.args = queue_impl::argsFor(*job.spec);
            request.input = reader.get();
            request.limits = limits;
            request.sink = [&writer](const std::span<const uint8_t> bytes) { writer->write(bytes); };
            request.stop = stop;

            const auto result = Runner::run(request);
            if (result.ok()) {
                // Commit only what was derived from the content the file still has.
                if (!queue_impl::sourceStillCurrent(key)) {
                    writer->abort();
                    log::Registry::thumb()->info(
                        "[preview::derive] {} for file {}: the source changed during the job; result discarded",
                        key.kind, key.file_id);
                    return queue_impl::Outcome::Discarded;
                }
                const auto artifact = writer->commit();
                log::Registry::thumb()->info("[preview::derive] {} for file {} (vault {}) ready: {} bytes", key.kind,
                                             key.file_id, key.vault_id, artifact.size);
                return queue_impl::Outcome::Completed;
            }
            reason = result.failureReason();
            message = result.failureMessage();
        } catch (const HelperUnavailable& e) {
            reason = "converter_unavailable";
            message = e.what();
        } catch (const storage::ContentUnavailable& e) {
            reason = "content_unavailable";
            message = e.what();
        } catch (const storage::IntegrityError& e) {
            reason = "integrity";
            message = e.what();
        } catch (const std::length_error& e) {
            reason = "limit_exceeded";   // the artifact writer's cap
            message = e.what();
        } catch (const std::exception& e) {
            reason = "internal";         // cache directory / DB / pipe trouble: says nothing about the file
            message = e.what();
        }
        if (writer) writer->abort();

        if (queue_impl::deterministic(reason)) {
            // A failure of a generation that is no longer current (e.g. a parse failure on bytes that were
            // replaced mid-read) says nothing about the new content.
            if (queue_impl::sourceStillCurrent(key)) {
                cache::Store::putFailure(job.engine, key, reason);
                warn(reason, message);
                return queue_impl::Outcome::Failed;
            }
            log::Registry::thumb()->info("[preview::derive] {} for file {}: {} after the source changed; not cached",
                                         key.kind, key.file_id, reason);
            return queue_impl::Outcome::Discarded;
        }

        transientReason = reason.empty() ? "internal" : reason;
        warn(transientReason, message);
        return queue_impl::Outcome::Failed;
    }
};

bool kindKnown(const std::string_view kind) { return queue_impl::specFor(kind) != nullptr; }

uint32_t generatorVersion(const std::string_view kind) {
    const auto* spec = queue_impl::specFor(kind);
    return spec ? spec->generatorVersion : 0;
}

std::string_view helperFor(const std::string_view kind) {
    const auto* spec = queue_impl::specFor(kind);
    return spec ? spec->helper : std::string_view{};
}

Queue& Queue::instance() {
    static Queue queue;
    return queue;
}

Queue::Queue() : impl_(std::make_unique<Impl>()) {}

Queue::~Queue() { shutdown(); }

DeriveResult Queue::request(const std::shared_ptr<storage::Engine>& engine,
                            const std::shared_ptr<fs::model::File>& file,
                            const std::string& kind,
                            const std::string& variant) {
    DeriveResult out;
    const auto* spec = queue_impl::specFor(kind);
    if (!spec) {
        out.reason = "unknown_kind";
        return out;
    }
    if (!engine || !engine->vault || !file || variant != queue_impl::kDefaultVariant ||
        !queue_impl::applicable(*spec, *file)) {
        out.reason = "not_applicable";
        return out;
    }
    if (spec->transcode && config::Registry::get().preview.media.transcode == config::PreviewTranscodeMode::Off) {
        out.reason = "transcode_disabled";
        return out;
    }
    if (!Runner::helperAvailable(spec->helper)) {
        out.status = DeriveStatus::Unavailable;
        out.reason = "converter_unavailable";
        out.helper = std::string(spec->helper);
        return out;
    }

    // A private copy: cached File models are shared and rewritten in place by overwrites, while the job must keep
    // describing the generation this key names.
    const auto snapshot = std::make_shared<fs::model::File>(*file);
    const cache::ArtifactKey key{
        .vault_id = engine->vault->id,
        .file_id = file->id,
        .kind = std::string(spec->kind),
        .variant = variant,
        .source_id = storage::generationOf(*snapshot).sourceId(),
        .generator_version = spec->generatorVersion
    };

    const auto found = cache::Store::lookup(engine, key);
    if (found.status == cache::LookupStatus::Ready) {
        out.status = DeriveStatus::Ready;
        out.artifact = found.artifact;
        return out;
    }
    if (found.status == cache::LookupStatus::Failed) {
        out.status = DeriveStatus::Failed;
        out.reason = found.failure;
        return out;
    }

    auto id = key.canonical();
    auto& s = *impl_;
    const std::scoped_lock lock(s.mutex);
    if (s.inflight.contains(id)) {
        out.status = DeriveStatus::Queued;
        return out;
    }
    if (const auto it = s.transient.find(id); it != s.transient.end()) {
        if (queue_impl::Clock::now() < it->second.second) {
            out.status = DeriveStatus::Failed;
            out.reason = it->second.first;
            return out;
        }
        s.transient.erase(it);
    }
    const auto maxQueue = std::max<uint32_t>(1, config::Registry::get().preview.derive.max_queue);
    if (s.stopping || s.pending.size() >= maxQueue) {
        ++s.rejectedBusy;
        out.status = DeriveStatus::Busy;
        out.reason = s.stopping ? "shutting_down" : "queue_full";
        return out;
    }

    // Bounded by the number of distinct keys tried recently; drop expired memos while the lock is held anyway.
    std::erase_if(s.transient, [now = queue_impl::Clock::now()](const auto& entry) { return entry.second.second <= now; });

    s.inflight.insert(id);
    s.pending.push_back(std::make_shared<queue_impl::Job>(
        queue_impl::Job{.engine = engine, .file = snapshot, .key = key, .spec = spec, .id = std::move(id)}));
    s.ensureWorkers();
    s.cv.notify_one();
    out.status = DeriveStatus::Queued;
    return out;
}

Queue::Stats Queue::stats() const {
    const std::scoped_lock lock(impl_->mutex);
    return {.queued = impl_->pending.size(), .running = impl_->running, .completed = impl_->completed,
            .failed = impl_->failed, .rejectedBusy = impl_->rejectedBusy};
}

void Queue::shutdown() {
    std::vector<std::jthread> workers;
    {
        const std::scoped_lock lock(impl_->mutex);
        if (impl_->workers.empty()) {
            impl_->pending.clear();
            impl_->inflight.clear();
            return;
        }
        impl_->stopping = true;
        workers = std::move(impl_->workers);
        impl_->workers.clear();
        for (auto& worker : workers) worker.request_stop();   // running helpers are SIGKILLed via RunRequest::stop
    }
    impl_->cv.notify_all();
    workers.clear();   // joins

    const std::scoped_lock lock(impl_->mutex);
    impl_->pending.clear();
    impl_->inflight.clear();
    impl_->transient.clear();
    impl_->stopping = false;   // a later request starts fresh workers
}

}
