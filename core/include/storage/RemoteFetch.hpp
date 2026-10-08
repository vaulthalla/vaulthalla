#pragma once

#include "fs/Fwd.hpp"
#include "storage/s3/RequestUsage.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// Cost controls for reading remote-only cloud objects through storage::Engine::openPlaintextReader (hydrate or the
// opt-in ranged reader). Two separate systems, as everywhere else in S3 cost safety: a *price* preflight (the price
// budget ledger: off|report|warn|enforce) before any body is fetched, and *request* caps (GET count, bytes) enforced
// per request through s3::Controller::recordRequest and a per-read ScopedS3RequestUsageCapture.
namespace vh::storage {

class CloudEngine;

// One remote read the price budget must approve before any body is fetched.
struct RemoteFetchRequest {
    std::filesystem::path path;   // vault path of the object
    std::string operation;        // ledger operation and usage source: "preview_hydrate" | "preview_ranged"
    uint64_t head_requests{};
    uint64_t get_requests{};
    uint64_t download_bytes{};
};

// A price-budget reservation for one remote read. The first commit() or release() takes effect; neither throws.
class RemoteFetchReservation {
public:
    virtual ~RemoteFetchReservation() = default;
    // Records what was actually fetched (an empty usage releases the reservation instead).
    virtual void commit(const s3::S3GatewayUpstreamUsage& actual) noexcept = 0;
    virtual void release() noexcept = 0;
};

// Price preflight for a remote read: a reservation, or ContentUnavailable when the price budget refuses.
using RemoteFetchGate =
    std::function<std::unique_ptr<RemoteFetchReservation>(const CloudEngine&, const RemoteFetchRequest&)>;

// The default gate: a BudgetConservative S3 price estimate plus PriceBudgetService::preflight over the
// global/provider/vault scopes, the machinery sync uses (so the policy modes apply as they do to sync). A refusal
// records the usual notifications and throws ContentUnavailable with the budget's reason. Needs the database.
[[nodiscard]] std::unique_ptr<RemoteFetchReservation> priceBudgetRemoteFetchGate(const CloudEngine& engine,
                                                                                const RemoteFetchRequest& request);

// Request caps for one opt-in ranged remote reader (preview.media.remote: ranged).
struct RangedReadLimits {
    uint64_t window_bytes{4ull * 1024 * 1024};   // every GET covers one aligned window of this size
    std::size_t cached_windows{4};               // LRU of ciphertext windows kept per reader
    std::optional<uint64_t> max_get_requests;     // default 2 * windows + 8
    std::optional<uint64_t> max_downloaded_bytes; // default 2 * (object size + window)
};

// Records a hydrated copy's encryption state when it differs from the files row: compare-and-set on the row (still
// expectedIv / expectedVersion) plus the fs cache refresh. False when the row changed meanwhile.
using HydrateCatalogCommit = std::function<bool(const ::vh::fs::model::File& updated, const std::string& expectedIv,
                                                unsigned int expectedVersion)>;

}
