#pragma once

#include "ops/Actor.hpp"
#include "db/model/ListQueryParams.hpp"
#include "vault/model/Vault.hpp"
#include "sync/Fwd.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Vault lifecycle and sync policy shared by `vh vault ...` and the ws storage.vault.* commands. Every change goes
// through storage::Manager so the live engine (which the RBAC resolver reads the owner from) never goes stale, and
// sync policy changes are copy-on-write, swapped under the engine lock, persisted and pushed to the controller.
namespace vh::ops::vaults {

using VaultPtr = std::shared_ptr<vault::model::Vault>;
using PolicyPtr = std::shared_ptr<sync::model::Policy>;

// Absent = unchanged; an empty inner optional clears the limit.
struct S3BudgetPatch {
    std::optional<std::optional<uint64_t>> list, head, get, put, copy, del, downloaded_bytes;
    [[nodiscard]] bool empty() const { return !list && !head && !get && !put && !copy && !del && !downloaded_bytes; }
};

// Sync settings as the caller spelled them; parsed and validated against the vault type by the op.
struct SyncPatch {
    std::optional<std::chrono::seconds> interval{};
    std::optional<bool> enabled{};
    std::optional<std::string> strategy{};          // S3 only: cache | sync | mirror
    std::optional<std::string> conflict_policy{};   // local: overwrite|keep_both|ask; S3: keep_local|keep_remote|keep_newest|ask
    std::optional<std::optional<std::chrono::seconds>> max_remote_index_age{};   // S3 only
    std::optional<std::string> s3_budget_preset{};  // S3 only, applied before s3_budget
    S3BudgetPatch s3_budget{};                      // S3 only
    [[nodiscard]] bool empty() const {
        return !interval && !enabled && !strategy && !conflict_policy && !max_remote_index_age && !s3_budget_preset &&
               s3_budget.empty();
    }
};

struct S3Spec {
    unsigned int api_key_id = 0;
    std::string bucket;
    std::optional<std::string> storage_tier{};
    std::optional<bool> encrypt_upstream{};          // default: encrypt
};

struct Create {
    std::string name;
    vault::model::VaultType type{vault::model::VaultType::Local};
    std::optional<unsigned int> owner_id{};          // default: the actor
    std::string description{};
    uintmax_t quota = 0;
    std::string slug{};
    std::optional<std::string> fuse_name{};
    std::optional<S3Spec> s3{};
    SyncPatch sync{};
    bool accept_waiver = false;                      // see NeedsConfirmation "encryption_waiver"
};

// Patch semantics: an absent field is unchanged.
struct Update {
    unsigned int id = 0;
    std::optional<std::string> name{};
    std::optional<std::string> description{};
    std::optional<uintmax_t> quota{};
    std::optional<unsigned int> owner_id{};          // reassignment also needs Create for the new owner
    std::optional<std::string> slug{};
    std::optional<std::optional<std::string>> fuse_name{};
    std::optional<bool> is_active{};
    std::optional<unsigned int> api_key_id{};        // needs Consume on the new key
    std::optional<std::string> bucket{};
    std::optional<std::optional<std::string>> storage_tier{};
    std::optional<bool> encrypt_upstream{};
    SyncPatch sync{};                                // needs vault sync.config.edit
    bool accept_waiver = false;
};

struct Details {
    VaultPtr vault;
    PolicyPtr sync;
    std::string owner_name;
};

enum class SyncStart { Started, RerunQueued };

// Throws NeedsConfirmation{"encryption_waiver"} when an S3 vault would be created over, or switch encryption on,
// a bucket that already holds data, unless accept_waiver is set. The accepted waiver is recorded.
[[nodiscard]] VaultPtr create(const Actor& actor, const Create& req);
[[nodiscard]] VaultPtr update(const Actor& actor, const Update& req);
VaultPtr remove(const Actor& actor, unsigned int vaultId);

// Checks only, nothing changes: what update {owner_id} and remove would refuse. Ownership transfer is an
// administrator's act: it needs an admin account, vault Edit, and the right to create vaults for the new owner, and
// the new owner must not already have a vault with that name.
void requireTransferable(const Actor& actor, unsigned int vaultId, unsigned int newOwnerId);
void requireRemovable(const Actor& actor, unsigned int vaultId);
[[nodiscard]] Details get(const Actor& actor, unsigned int vaultId);
[[nodiscard]] std::vector<VaultPtr> list(const Actor& actor, db::model::ListQueryParams params = {},
                                         std::optional<vault::model::VaultType> type = std::nullopt);
[[nodiscard]] PolicyPtr updateSync(const Actor& actor, unsigned int vaultId, const SyncPatch& patch);
SyncStart triggerSync(const Actor& actor, unsigned int vaultId);

}
