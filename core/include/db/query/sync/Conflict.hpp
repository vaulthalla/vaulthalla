#pragma once

#include "sync/Fwd.hpp"
#include "sync/model/Baseline.hpp"

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Open sync conflicts and per-file sync baselines (#187, migration 107). Trusted primitives: nothing here
// authorizes; ops::sync does.
//
// One open (resolution = 'unresolved') conflict per file, enforced by uq_sync_conflicts_open_file. A pass records
// it once and refreshes its artifacts in place when a side changes again; it is closed by a decision
// (kept_local / kept_remote), by the policy once it is no longer `ask`, as 'converged' when both sides became
// equal again, or as 'superseded' when one side went away.
namespace vh::db::query::sync {

struct ConflictSide {
    uint64_t size_bytes{};
    std::optional<std::string> mime_type;
    std::optional<std::string> content_hash;
    std::optional<std::time_t> modified_at;
    std::optional<std::string> etag;          // remote side
    std::optional<bool> encrypted;            // remote side: Vaulthalla-encrypted upstream
    std::optional<std::string> encryption_iv;
    std::optional<unsigned int> key_version;
};

struct ConflictReasonRow {
    std::string code;
    std::string message;
};

struct ConflictRecord {
    uint32_t id{};
    uint32_t vault_id{};
    uint32_t file_id{};
    std::optional<uint32_t> event_id;
    std::string path;                         // vault path of the file ("/docs/a.txt")
    std::string name;
    std::string type{"mismatch"};
    std::string resolution{"unresolved"};
    std::time_t created_at{};
    std::time_t updated_at{};
    std::optional<std::time_t> resolved_at;
    ConflictSide local;
    ConflictSide remote;
    std::vector<ConflictReasonRow> reasons;

    [[nodiscard]] bool open() const noexcept { return resolution == "unresolved"; }
};

// What one sync pass decided about conflicts and baselines; written in one transaction after planning.
struct ConflictPassWrites {
    std::vector<vh::sync::model::Baseline> baselines;
    std::vector<std::shared_ptr<vh::sync::model::Conflict>> open;          // new, or artifacts changed
    std::vector<std::pair<uint32_t, std::string>> close;                    // file_id -> resolution

    [[nodiscard]] bool empty() const noexcept { return baselines.empty() && open.empty() && close.empty(); }
};

class Conflict {
public:
    [[nodiscard]] static std::unordered_map<uint32_t, vh::sync::model::Baseline> baselinesForVault(uint32_t vaultId);
    static void upsertBaseline(uint32_t vaultId, const vh::sync::model::Baseline& baseline);

    // Open conflicts of one vault, keyed by file id.
    [[nodiscard]] static std::unordered_map<uint32_t, ConflictRecord> openForVault(uint32_t vaultId);
    // Open conflicts in the given vaults (all vaults when nullopt), newest first; deleted vaults excluded.
    [[nodiscard]] static std::vector<ConflictRecord> listOpen(const std::optional<std::vector<uint32_t>>& vaultIds);
    // Open conflict counts per live vault: (vault_id, count), vaults with none omitted.
    [[nodiscard]] static std::vector<std::pair<uint32_t, uint64_t>> openCountsByVault();
    // Any conflict by id (open or closed), or nullopt.
    [[nodiscard]] static std::optional<ConflictRecord> get(uint32_t id);

    static void applyPass(uint32_t vaultId, std::optional<uint32_t> eventId, const ConflictPassWrites& writes);

    // Closes an open conflict with `resolution` and records the file's new baseline in the same transaction.
    // False when the conflict was no longer open (someone else closed it).
    static bool finishResolution(uint32_t conflictId, const std::string& resolution,
                                 const std::optional<vh::sync::model::Baseline>& baseline, uint32_t vaultId);
};

[[nodiscard]] ConflictSide sideFrom(const vh::sync::model::Conflict& conflict, bool local);
// Whether a recorded side still describes `file` (same content identity), so a decision about it is not stale.
[[nodiscard]] bool sideMatches(const ConflictSide& side, const std::optional<std::string>& hash, uint64_t size,
                               const std::optional<std::string>& etag);

}
