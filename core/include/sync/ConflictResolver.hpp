#pragma once

#include "db/query/sync/Conflict.hpp"
#include "storage/Fwd.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Carries out a decision on an open sync conflict (#187). A trusted primitive: callers (ops::conflicts) authorize.
//
// keep_local uploads the local copy over the remote object; keep_remote downloads the remote object over the local
// copy, decrypted and re-sealed exactly as a sync download is. Both:
//   - refuse a stale decision: the local row must still be the recorded local artifact and the bucket must still
//     hold the recorded remote version (one HEAD: ETag, else content-hash metadata, else size); keep_remote also
//     GETs with If-Match and replaces the local copy only while it is still the generation that was checked;
//   - are price-preflighted like a sync run (operation "conflict_resolve") and metered under the vault's S3 request
//     budget through a per-thread usage capture (never the engine-wide budget a running pass owns);
//   - take no FS or DB lock across the network work: DB reads before, one short transaction after (close the
//     conflict + record the new baseline);
//   - run at most once at a time per conflict in this process.
namespace vh::sync {

enum class ConflictDecision { KeepLocal, KeepRemote };

[[nodiscard]] const char* toString(ConflictDecision d);          // "keep_local" | "keep_remote"
[[nodiscard]] const char* resolutionFor(ConflictDecision d);     // "kept_local" | "kept_remote"

// A side changed since the conflict was recorded, or the conflict was closed meanwhile.
struct ConflictStale final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ConflictResolver {
public:
    // Throws ConflictStale, storage::ContentUnavailable (price/request budget, archive tier, object gone),
    // std::invalid_argument (not a remote vault, no such file) or another exception (a fault).
    static void resolve(const std::shared_ptr<storage::CloudEngine>& engine,
                        const db::query::sync::ConflictRecord& conflict, ConflictDecision decision);

    // The remote side's current plaintext for a preview, at most maxBytes (ContentTooLarge beyond). Price-preflighted
    // as "conflict_preview", one HEAD + one GET (If-Match), never written to disk.
    struct ContentTooLarge final : std::runtime_error {
        uint64_t limit{};
        ContentTooLarge(const std::string& what, const uint64_t l) : std::runtime_error(what), limit(l) {}
    };
    [[nodiscard]] static std::vector<uint8_t> fetchRemoteForPreview(const std::shared_ptr<storage::CloudEngine>& engine,
                                                                    const db::query::sync::ConflictRecord& conflict,
                                                                    uint64_t maxBytes);
};

}
