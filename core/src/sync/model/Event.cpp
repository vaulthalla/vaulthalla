#include "sync/model/Event.hpp"
#include "db/encoding/timestamp.hpp"
#include "db/query/sync/Event.hpp"
#include "log/Registry.hpp"
#include "sync/model/Conflict.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <pqxx/row>
#include <pqxx/result>
#include <nlohmann/json.hpp>

namespace {
    std::string lowerCopy(std::string_view sv) {
        std::string s(sv);
        std::ranges::transform(s.begin(), s.end(), s.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Safer helper: returns 0 when NULL.
    template <typename T>
    T as_or_default(const pqxx::row& r, const char* col, T def) {
        const auto f = r[col];
        return f.is_null() ? def : f.as<T>();
    }

    // Safer helper: returns empty string when NULL.
    std::string as_or_empty(const pqxx::row& r, const char* col) {
        const auto f = r[col];
        return f.is_null() ? std::string{} : f.as<std::string>();
    }
}

using namespace vh::sync::model;
using namespace vh::db::encoding;
using namespace std::chrono;

Event::Event(const pqxx::row& row)
    : id(row["id"].as<uint32_t>())
    , vault_id(row["vault_id"].as<uint32_t>())
    , run_uuid(row["run_uuid"].as<std::string>())
    , timestamp_begin(parsePostgresTimestamp(row["timestamp_begin"].as<std::string>()))
    , timestamp_end(row["timestamp_end"].is_null()
        ? 0
        : parsePostgresTimestamp(row["timestamp_end"].as<std::string>()))
    , heartbeat_at(row["heartbeat_at"].is_null()
        ? 0
        : parsePostgresTimestamp(row["heartbeat_at"].as<std::string>()))
    , retry_attempt(as_or_default<uint32_t>(row, "retry_attempt", 0))
    , stall_reason(as_or_empty(row, "stall_reason"))
    , error_code(as_or_empty(row, "error_code"))
    , error_message(as_or_empty(row, "error_message"))
    , num_ops_total(as_or_default<std::uint64_t>(row, "num_ops_total", 0))
    , num_failed_ops(as_or_default<std::uint64_t>(row, "num_failed_ops", 0))
    , num_conflicts(as_or_default<std::uint64_t>(row, "num_conflicts", 0))
    , bytes_up(as_or_default<std::uint64_t>(row, "bytes_up", 0))
    , bytes_down(as_or_default<std::uint64_t>(row, "bytes_down", 0))
    , divergence_detected(as_or_default<bool>(row, "divergence_detected", false))
    , local_state_hash(as_or_empty(row, "local_state_hash"))
    , remote_state_hash(as_or_empty(row, "remote_state_hash"))
    , config_hash(as_or_empty(row, "config_hash"))
{
    {
        if (auto s = Status::RUNNING; tryParseState(as_or_empty(row, "status"), s)) status = s;
    }
    {
        if (auto t = Trigger::SCHEDULE; tryParseTrigger(as_or_empty(row, "trigger"), t)) trigger = t;
    }
}

void Event::start() {
    timestamp_begin = system_clock::to_time_t(system_clock::now());
}

void Event::stop() {
    timestamp_end = system_clock::to_time_t(system_clock::now());
}

void Event::heartbeat(std::time_t min_interval_seconds) {
    const std::time_t now = std::time(nullptr);
    heartbeat_at = now;

    // only persist if enough time passed
    if (last_heartbeat_persisted_at != 0 &&
        now - last_heartbeat_persisted_at < min_interval_seconds)
        return;

    db::query::sync::Event::heartbeat(shared_from_this());
    last_heartbeat_persisted_at = now;
}

void Event::parseCurrentStatus() {
    // ---- Tunables (keep local for MVP; later pull from config) ----
    constexpr std::time_t kStallAfterSeconds = 90;  // heartbeat silence threshold
    constexpr bool kTreatDivergenceAsError   = false; // flip true if you want divergence to hard-fail
    constexpr bool kFailedOpsRequireEnd      = false; // flip true if failed ops during run shouldn't mark ERROR yet

    // Preserve terminal states. Once you're done, you're done.
    switch (status) {
        case Status::SUCCESS:
        case Status::ERROR:
        case Status::CANCELLED:
            return;
        default:
            break;
    }

    // If you haven't started, don't invent drama.
    if (timestamp_begin == 0) {
        status = Status::PENDING;
        return;
    }

    computeDashboardStats();

    const std::time_t now = std::time(nullptr);
    const bool ended = (timestamp_end != 0);

    // -------------------------
    // 1) ERROR detection (highest priority)
    // -------------------------
    const bool hasExplicitError =
        (!error_code.empty()) || (!error_message.empty());

    const bool hasFailedOps =
        (num_failed_ops > 0);

    const bool divergenceError =
        kTreatDivergenceAsError && divergence_detected;

    const bool errorByFailures =
        hasFailedOps && (!kFailedOpsRequireEnd || ended);

    if (hasExplicitError || divergenceError || errorByFailures) {
        status = Status::ERROR;
        return;
    }

    // -------------------------
    // 2) STALLED detection (only meaningful if not ended)
    // -------------------------
    if (!ended) {
        // If heartbeat exists and is stale → stalled
        if (heartbeat_at != 0) {
            const std::time_t dt = (now > heartbeat_at) ? (now - heartbeat_at) : 0;
            if (dt >= kStallAfterSeconds) {
                status = Status::STALLED;
                if (stall_reason.empty()) {
                    stall_reason = "heartbeat timeout";
                }
                return;
            }
        }

        // If we are mid-run and not errored/stalled, we're RUNNING.
        status = Status::RUNNING;
        return;
    }

    // -------------------------
    // 3) SUCCESS (ended, no error, no stall)
    // -------------------------
    status = Status::SUCCESS;
}

void Event::addThroughput(std::unique_ptr<Throughput> t) {
    if (!t) return;
    throughputs.emplace_back(std::move(t));
}

const Throughput* Event::getThroughput(const Throughput::Metric& metric) const noexcept {
    const auto m = static_cast<Throughput::Metric>(metric);
    for (const auto& p : throughputs) {
        if (p && p->metric_type == m) return p.get();
    }
    return nullptr;
}

Throughput* Event::getThroughput(const Throughput::Metric& metric) noexcept {
    const auto m = static_cast<Throughput::Metric>(metric);
    for (auto& p : throughputs) if (p && p->metric_type == m) return p.get();
    return nullptr;
}

Throughput& Event::getOrCreateThroughput(const Throughput::Metric& metric) {
    if (auto* existing = getThroughput(metric)) return *existing;

    auto created = std::make_unique<Throughput>();
    created->run_uuid = run_uuid;
    created->metric_type = static_cast<Throughput::Metric>(metric);

    throughputs.emplace_back(std::move(created));
    return *throughputs.back();
}

void Event::computeDashboardStats() {
    // Reset derived values (leave num_failed_ops/num_conflicts as they may be tracked elsewhere too)
    num_ops_total = 0;
    bytes_up = 0;
    bytes_down = 0;

    for (const auto& p : throughputs) {
        if (!p) continue;

        p->computeDashboardStats();

        num_ops_total += p->num_ops;
        num_failed_ops += p->failed_ops;

        switch (p->metric_type) {
            case Throughput::UPLOAD:
                bytes_up += p->size_bytes;
                break;
            case Throughput::DOWNLOAD:
                bytes_down += p->size_bytes;
                break;
            default:
                break;
        }
    }
}

// -------------------------
// Enum ↔ string
// -------------------------
std::string_view Event::toString(Status s) noexcept {
    switch (s) {
        case Status::RUNNING:   return "running";
        case Status::SUCCESS:   return "success";
        case Status::STALLED:   return "stalled";
        case Status::ERROR:     return "error";
        case Status::CANCELLED: return "cancelled";
        case Status::PENDING:   return "pending";
        default:               return "running";
    }
}

std::string_view Event::toString(Trigger t) noexcept {
    switch (t) {
        case Trigger::SCHEDULE: return "schedule";
        case Trigger::MANUAL:   return "manual";
        case Trigger::STARTUP:  return "startup";
        case Trigger::WEBHOOK:  return "webhook";
        case Trigger::RETRY:    return "retry";
        default:                return "schedule";
    }
}

bool Event::tryParseState(std::string_view in, Status& out) noexcept {
    const std::string s = lowerCopy(in);
    if (s == "running")   { out = Status::RUNNING; return true; }
    if (s == "success")   { out = Status::SUCCESS; return true; }
    if (s == "stalled")   { out = Status::STALLED; return true; }
    if (s == "error")     { out = Status::ERROR; return true; }
    if (s == "cancelled") { out = Status::CANCELLED; return true; }
    if (s == "canceled")  { out = Status::CANCELLED; return true; }
    if (s == "pending")   { out = Status::PENDING; return true; }
    return false;
}

bool Event::tryParseTrigger(std::string_view in, Trigger& out) noexcept {
    const std::string s = lowerCopy(in);
    if (s == "schedule" || s == "scheduled") { out = Trigger::SCHEDULE; return true; }
    if (s == "manual")                       { out = Trigger::MANUAL; return true; }
    if (s == "startup")                      { out = Trigger::STARTUP; return true; }
    if (s == "webhook")                      { out = Trigger::WEBHOOK; return true; }
    if (s == "retry")                        { out = Trigger::RETRY; return true; }
    return false;
}

pqxx::params Event::getParams() const noexcept {
    return {
        vault_id,
        run_uuid,

        timestampToString(timestamp_begin),
        timestampToString(timestamp_end),

        std::string(toString(status)),
        std::string(toString(trigger)),
        retry_attempt,

        timestampToString(heartbeat_at),

        stall_reason,
        error_code,
        error_message,

        num_ops_total,
        num_failed_ops,
        num_conflicts,
        bytes_up,
        bytes_down,

        divergence_detected,
        local_state_hash,
        remote_state_hash,

        config_hash
    };
}

// -------------------------
// JSON
// -------------------------
void vh::sync::model::to_json(nlohmann::json& j, const std::shared_ptr<Event>& e) {
    if (!e) {
        j = nullptr;
        return;
    }

    j = {
        {"id", e->id},
        {"vault_id", e->vault_id},
        {"run_uuid", e->run_uuid},

        {"timestamp_begin", e->timestamp_begin},
        {"timestamp_end", e->timestamp_end},
        {"heartbeat_at", e->heartbeat_at},

        {"status", std::string(Event::toString(e->status))},
        {"trigger", std::string(Event::toString(e->trigger))},
        {"retry_attempt", e->retry_attempt},

        {"stall_reason", e->stall_reason},
        {"error_code", e->error_code},
        {"error_message", e->error_message},

        {"num_ops_total", e->num_ops_total},
        {"num_failed_ops", e->num_failed_ops},
        {"num_conflicts", e->num_conflicts},
        {"bytes_up", e->bytes_up},
        {"bytes_down", e->bytes_down},

        {"conflicts", e->conflicts},
        {"throughputs", e->throughputs},

        {"divergence_detected", e->divergence_detected},
        {"local_state_hash", e->local_state_hash},
        {"remote_state_hash", e->remote_state_hash},

        {"config_hash", e->config_hash},
    };
}

std::vector<std::shared_ptr<Event>> vh::sync::model::sync_events_from_pqxx_res(const pqxx::result& res) {
    std::vector<std::shared_ptr<Event>> events;
    for (const auto& row : res) {
        try {
            auto event = std::make_shared<Event>(row);
            events.push_back(std::move(event));
        } catch (const std::exception& ex) {
            log::Registry::types()->error("Failed to parse Event from database row: {}", ex.what());
        }
    }
    return events;
}
