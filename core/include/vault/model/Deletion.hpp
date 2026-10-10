#pragma once

#include "vault/Fwd.hpp"

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include "db/Fwd.hpp"

namespace vh::vault::model {

// One deleted vault (#162, table vault_deletion): its schedule, the choices made at delete time, purge progress and,
// once purged, the tombstone that outlives the vault row.
//   Pending  deleted and restorable; purged once purge_after passes (or at once for "delete now").
//   Purging  the retention service has started removing its data (upstream first, when chosen); resumes after a
//            restart. No longer restorable.
//   Purged   data and vault row gone. The sealed key is kept until key_retain_until (key_purged_at then set).
enum class DeletionState { Pending, Purging, Purged };

[[nodiscard]] std::string to_string(DeletionState state);
[[nodiscard]] DeletionState deletionStateFromString(const std::string& state);

struct Deletion {
    uint32_t vault_id{};
    std::string vault_name;
    std::optional<uint32_t> owner_id{};
    std::string owner_name;
    VaultType vault_type{};
    std::string backing_alias;                      // vault.mount_point: the backing and cache directory name
    std::optional<std::string> provider{}, bucket{};
    std::optional<bool> encrypt_upstream{};
    bool delete_upstream{false};
    std::optional<uint32_t> deleted_by{};
    std::time_t deleted_at{}, purge_after{}, key_retain_until{};
    std::optional<unsigned int> key_version{};
    std::optional<std::time_t> key_exported_at{};   // the current key version had been exported
    DeletionState state{DeletionState::Pending};
    std::optional<std::time_t> purge_started_at{}, upstream_purged_at{}, purged_at{}, key_purged_at{}, next_attempt_at{};
    unsigned int attempts{};
    std::optional<std::string> last_error{};

    Deletion() = default;
    explicit Deletion(pqxx::row_ref row);

    [[nodiscard]] bool isS3() const;
    [[nodiscard]] bool restorable() const { return state == DeletionState::Pending; }
    [[nodiscard]] bool keyRetained() const { return !key_purged_at.has_value(); }
    [[nodiscard]] bool keyExported() const { return key_exported_at.has_value(); }
    // Encrypted objects stay in the bucket under a key nobody has exported: the data-loss case the delete flows and
    // the deleted-vault listings keep warning about until the key is exported (or its retention ends).
    [[nodiscard]] bool upstreamKeyAtRisk() const;
};

using DeletionPtr = std::shared_ptr<Deletion>;

// The key export command the delete flows and deleted-vault listings point at (works before and after deletion,
// until the key retention window ends).
[[nodiscard]] std::string keyExportCommand(uint32_t vaultId);

void to_json(nlohmann::json& j, const Deletion& d);
void to_json(nlohmann::json& j, const std::vector<DeletionPtr>& deletions);
[[nodiscard]] std::string to_string(const std::vector<DeletionPtr>& deletions);

}
