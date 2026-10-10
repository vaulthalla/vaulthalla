#include "vault/model/Deletion.hpp"
#include "vault/model/Vault.hpp"
#include "db/encoding/has.hpp"
#include "db/encoding/timestamp.hpp"
#include "protocols/shell/Table.hpp"
#include "protocols/shell/util/lineHelpers.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/row>
#include <stdexcept>

namespace vh::vault::model {

namespace {

std::optional<std::time_t> optionalTime(pqxx::row_ref row, const char* column) {
    if (row[column].is_null()) return std::nullopt;
    return db::encoding::parsePostgresTimestamp(row[column].c_str());
}

nlohmann::json timeOrNull(const std::optional<std::time_t>& t) {
    return t ? nlohmann::json(db::encoding::timestampToString(*t)) : nlohmann::json(nullptr);
}

template<class T>
nlohmann::json valueOrNull(const std::optional<T>& v) {
    return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
}

std::string stateLabel(const Deletion& d) {
    switch (d.state) {
        case DeletionState::Pending: return "pending (restorable)";
        case DeletionState::Purging: return d.last_error ? "purging (retrying)" : "purging";
        case DeletionState::Purged: return d.keyRetained() ? "purged (key retained)" : "purged";
    }
    return "unknown";
}

}

std::string to_string(const DeletionState state) {
    switch (state) {
        case DeletionState::Pending: return "pending";
        case DeletionState::Purging: return "purging";
        case DeletionState::Purged: return "purged";
    }
    return "pending";
}

DeletionState deletionStateFromString(const std::string& state) {
    if (state == "pending") return DeletionState::Pending;
    if (state == "purging") return DeletionState::Purging;
    if (state == "purged") return DeletionState::Purged;
    throw std::invalid_argument("unknown vault deletion state: " + state);
}

Deletion::Deletion(pqxx::row_ref row)
    : vault_id(row["vault_id"].as<uint32_t>()),
      vault_name(row["vault_name"].as<std::string>()),
      owner_id(db::encoding::try_get<uint32_t>(row, "owner_id")),
      owner_name(db::encoding::try_get<std::string>(row, "owner_name").value_or("")),
      vault_type(from_string(row["vault_type"].as<std::string>())),
      backing_alias(row["backing_alias"].as<std::string>()),
      provider(db::encoding::try_get<std::string>(row, "provider")),
      bucket(db::encoding::try_get<std::string>(row, "bucket")),
      encrypt_upstream(db::encoding::try_get<bool>(row, "encrypt_upstream")),
      delete_upstream(row["delete_upstream"].as<bool>()),
      deleted_by(db::encoding::try_get<uint32_t>(row, "deleted_by")),
      deleted_at(db::encoding::parsePostgresTimestamp(row["deleted_at"].c_str())),
      purge_after(db::encoding::parsePostgresTimestamp(row["purge_after"].c_str())),
      key_retain_until(db::encoding::parsePostgresTimestamp(row["key_retain_until"].c_str())),
      key_version(db::encoding::try_get<unsigned int>(row, "key_version")),
      key_exported_at(optionalTime(row, "key_exported_at")),
      state(deletionStateFromString(row["state"].as<std::string>())),
      purge_started_at(optionalTime(row, "purge_started_at")),
      upstream_purged_at(optionalTime(row, "upstream_purged_at")),
      purged_at(optionalTime(row, "purged_at")),
      key_purged_at(optionalTime(row, "key_purged_at")),
      next_attempt_at(optionalTime(row, "next_attempt_at")),
      attempts(row["attempts"].as<unsigned int>()),
      last_error(db::encoding::try_get<std::string>(row, "last_error")) {}

std::string keyExportCommand(const uint32_t vaultId) {
    const auto id = std::to_string(vaultId);
    return "vh vault keys export " + id + " --recipient <GPG fingerprint> --output /var/lib/vaulthalla/vault-" + id +
           "-key.gpg";
}

bool Deletion::isS3() const { return vault_type == VaultType::S3; }

bool Deletion::upstreamKeyAtRisk() const {
    return isS3() && encrypt_upstream.value_or(true) && !delete_upstream && !keyExported() && keyRetained();
}

void to_json(nlohmann::json& j, const Deletion& d) {
    j = {
        {"vault_id", d.vault_id},
        {"vault_name", d.vault_name},
        {"owner_id", valueOrNull(d.owner_id)},
        {"owner", d.owner_name},
        {"type", to_string(d.vault_type)},
        {"provider", valueOrNull(d.provider)},
        {"bucket", valueOrNull(d.bucket)},
        {"encrypt_upstream", valueOrNull(d.encrypt_upstream)},
        {"delete_upstream", d.delete_upstream},
        {"deleted_by", valueOrNull(d.deleted_by)},
        {"deleted_at", db::encoding::timestampToString(d.deleted_at)},
        {"purge_after", db::encoding::timestampToString(d.purge_after)},
        {"key_retain_until", db::encoding::timestampToString(d.key_retain_until)},
        {"key_version", valueOrNull(d.key_version)},
        {"key_exported_at", timeOrNull(d.key_exported_at)},
        {"key_retained", d.keyRetained()},
        {"upstream_key_at_risk", d.upstreamKeyAtRisk()},
        {"export_command", keyExportCommand(d.vault_id)},
        {"state", to_string(d.state)},
        {"restorable", d.restorable()},
        {"purge_started_at", timeOrNull(d.purge_started_at)},
        {"upstream_purged_at", timeOrNull(d.upstream_purged_at)},
        {"purged_at", timeOrNull(d.purged_at)},
        {"key_purged_at", timeOrNull(d.key_purged_at)},
        {"next_attempt_at", timeOrNull(d.next_attempt_at)},
        {"attempts", d.attempts},
        {"last_error", valueOrNull(d.last_error)}
    };
}

void to_json(nlohmann::json& j, const std::vector<DeletionPtr>& deletions) {
    j = nlohmann::json::array();
    for (const auto& d : deletions)
        if (d) j.push_back(*d);
}

std::string to_string(const std::vector<DeletionPtr>& deletions) {
    using protocols::shell::Align;
    protocols::shell::Table tbl({
        {"ID", Align::Right, 3, 8, false, false},
        {"NAME", Align::Left, 4, 48, false, false},
        {"OWNER", Align::Left, 5, 24, false, false},
        {"TYPE", Align::Left, 4, 8, false, false},
        {"STATE", Align::Left, 5, 24, false, false},
        {"PURGE AFTER", Align::Left, 11, 24, false, false},
        {"KEY KEPT UNTIL", Align::Left, 14, 24, false, false},
    }, protocols::shell::term_width());

    std::string warnings;
    for (const auto& d : deletions) {
        if (!d) continue;
        tbl.add_row({
            std::to_string(d->vault_id),
            d->vault_name,
            d->owner_name.empty() ? "N/A" : d->owner_name,
            to_string(d->vault_type),
            stateLabel(*d),
            d->state == DeletionState::Purged ? "-" : db::encoding::timestampToString(d->purge_after),
            d->keyRetained() ? db::encoding::timestampToString(d->key_retain_until) : "expired"
        });
        if (d->last_error)
            warnings += "  " + d->vault_name + " (ID " + std::to_string(d->vault_id) + "): purge attempt " +
                        std::to_string(d->attempts) + " failed: " + *d->last_error + "\n";
        if (d->upstreamKeyAtRisk())
            warnings += "  WARNING: " + d->vault_name + " (ID " + std::to_string(d->vault_id) + ") left encrypted data in bucket '" +
                        d->bucket.value_or("?") + "' and its key was never exported. Without the key that data can never be "
                        "decrypted. Export it before " + db::encoding::timestampToString(d->key_retain_until) +
                        ":\n    " + keyExportCommand(d->vault_id) + "\n";
    }

    std::string out = "deleted vaults:\n" + tbl.render();
    if (deletions.empty()) out += "\nNo deleted vaults\n";
    if (!warnings.empty()) out += "\n" + warnings;
    return out;
}

}
