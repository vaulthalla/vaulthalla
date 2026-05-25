#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/util/argsHelpers.hpp"
#include "runtime/Deps.hpp"
#include "sync/Controller.hpp"
#include "sync/Cloud.hpp"
#include "sync/Planner.hpp"
#include "storage/Engine.hpp"
#include "identities/User.hpp"
#include "vault/model/Vault.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/Policy.hpp"
#include "db/encoding/interval.hpp"
#include "db/query/vault/Vault.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "db/encoding/timestamp.hpp"
#include "CommandUsage.hpp"
#include "rbac/resolver/vault/all.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/ScopedS3RequestBudget.hpp"
#include "fs/model/File.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/RemoteManifest.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

using namespace vh;
using namespace vh::protocols::shell;
using namespace vh::protocols::shell::commands::vault;
using namespace vh::vault::model;
using namespace vh::storage;
using namespace vh::sync::model;
using namespace vh::db::encoding;

namespace {
    constexpr std::array<std::string_view, 20> DEFAULT_S3_INVENTORY_SCHEMA = {
        "bucket",
        "key",
        "version_id",
        "is_latest",
        "is_delete_marker",
        "size",
        "last_modified_date",
        "etag",
        "storage_class",
        "is_multipart_uploaded",
        "replication_status",
        "encryption_status",
        "object_lock_retain_until_date",
        "object_lock_retention_mode",
        "object_lock_legal_hold_status",
        "intelligent_tiering_access_tier",
        "bucket_key_status",
        "checksum_algorithm",
        "object_access_control_list",
        "object_owner"
    };

    std::string trim(std::string value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        return value;
    }

    std::string stripQuotes(std::string value) {
        value = trim(std::move(value));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            return value.substr(1, value.size() - 2);
        return value;
    }

    std::string canonicalColumn(std::string value) {
        value = trim(std::move(value));
        std::string out;
        out.reserve(value.size());
        for (const auto c : value) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return out;
    }

    bool truthy(std::string value) {
        value = canonicalColumn(std::move(value));
        return value == "true" || value == "1" || value == "yes";
    }

    std::vector<std::string> parseCsvRow(const std::string& line) {
        std::vector<std::string> fields;
        std::string current;
        bool quoted = false;

        for (size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (c == '"') {
                if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else quoted = !quoted;
            } else if (c == ',' && !quoted) {
                fields.push_back(trim(std::move(current)));
                current.clear();
            } else current.push_back(c);
        }

        fields.push_back(trim(std::move(current)));
        return fields;
    }

    std::vector<std::string> parseSchema(const std::string& schema) {
        std::vector<std::string> columns;
        std::string token;
        std::istringstream input(schema);
        while (std::getline(input, token, ',')) columns.push_back(canonicalColumn(token));
        return columns;
    }

    std::vector<std::string> defaultInventorySchema() {
        std::vector<std::string> schema;
        schema.reserve(DEFAULT_S3_INVENTORY_SCHEMA.size());
        for (const auto column : DEFAULT_S3_INVENTORY_SCHEMA)
            schema.emplace_back(canonicalColumn(std::string(column)));
        return schema;
    }

    std::optional<size_t> findColumn(
        const std::vector<std::string>& columns,
        const std::initializer_list<std::string_view> aliases) {
        for (const auto alias : aliases) {
            const auto canonical = canonicalColumn(std::string(alias));
            const auto it = std::ranges::find(columns, canonical);
            if (it != columns.end()) return static_cast<size_t>(std::distance(columns.begin(), it));
        }
        return std::nullopt;
    }

    std::string fieldAt(const std::vector<std::string>& row, const std::optional<size_t> index) {
        if (!index || *index >= row.size()) return {};
        return stripQuotes(row[*index]);
    }

    std::optional<uint64_t> parseUint64(const std::string& value) {
        const auto trimmed = trim(value);
        if (trimmed.empty()) return std::nullopt;
        uint64_t parsed = 0;
        for (const auto c : trimmed) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
            parsed = (parsed * 10) + static_cast<uint64_t>(c - '0');
        }
        return parsed;
    }

    std::optional<std::time_t> parseObjectTime(const std::string& value) {
        if (trim(value).empty()) return std::nullopt;
        try {
            return parsePostgresTimestamp(value);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::vector<std::shared_ptr<vh::fs::model::File>> parseInventoryCsv(
        const std::filesystem::path& filePath,
        const std::optional<std::string>& schemaOpt,
        const bool forceHeader,
        size_t& skippedRows) {
        std::ifstream input(filePath);
        if (!input) throw std::runtime_error("unable to open inventory CSV: " + filePath.string());

        std::vector<std::string> columns;
        std::vector<std::shared_ptr<vh::fs::model::File>> files;
        bool columnsReady = false;

        if (schemaOpt) {
            columns = parseSchema(*schemaOpt);
            columnsReady = true;
        }

        const auto keyIdxFor = [&]() { return findColumn(columns, {"key", "object_key", "objectkey"}); };
        const auto sizeIdxFor = [&]() { return findColumn(columns, {"size", "size_bytes", "sizebytes"}); };

        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (trim(line).empty()) continue;

            auto row = parseCsvRow(line);
            if (!columnsReady) {
                auto header = row;
                for (auto& column : header) column = canonicalColumn(column);
                const bool looksLikeHeader =
                    findColumn(header, {"key", "object_key", "objectkey"}) &&
                    findColumn(header, {"size", "size_bytes", "sizebytes"});

                if (forceHeader || looksLikeHeader) {
                    columns = std::move(header);
                    columnsReady = true;
                    continue;
                }

                columns = defaultInventorySchema();
                columnsReady = true;
            }

            const auto keyIdx = keyIdxFor();
            const auto sizeIdx = sizeIdxFor();
            if (!keyIdx || !sizeIdx) throw std::runtime_error("inventory schema must include key and size columns");

            const auto deleteMarker = fieldAt(row, findColumn(columns, {"is_delete_marker", "isdeletemarker"}));
            if (truthy(deleteMarker)) {
                ++skippedRows;
                continue;
            }

            const auto key = fieldAt(row, keyIdx);
            const auto size = parseUint64(fieldAt(row, sizeIdx));
            if (key.empty() || !size || remote_manifest::isVaulthallaManifestKey(key)) {
                ++skippedRows;
                continue;
            }

            auto file = std::make_shared<vh::fs::model::File>(
                key,
                *size,
                parseObjectTime(fieldAt(row, findColumn(columns, {
                    "last_modified_date",
                    "lastmodifieddate",
                    "last_modified",
                    "lastmodified"
                }))));

            if (auto etag = fieldAt(row, findColumn(columns, {"etag", "e_tag"})); !etag.empty())
                file->remote_etag = etag;
            if (auto version = fieldAt(row, findColumn(columns, {"version_id", "versionid"})); !version.empty())
                file->remote_version_id = version;

            if (auto storageClass = fieldAt(row, findColumn(columns, {"storage_class", "storageclass"}));
                !storageClass.empty())
                file->remote_storage_class = storageClass;

            if (auto tier = fieldAt(row, findColumn(columns, {
                    "intelligent_tiering_access_tier",
                    "intelligenttieringaccesstier"
                }));
                !tier.empty()) {
                if (!file->remote_storage_class) file->remote_storage_class = "INTELLIGENT_TIERING";
                const auto canonicalTier = canonicalColumn(tier);
                if (canonicalTier != "frequentaccess" && canonicalTier != "frequent")
                    file->remote_restore_status = "IntelligentTieringAccessTier=" + tier;
            }

            files.push_back(std::move(file));
        }

        return files;
    }

    int hexValue(const char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string decodeS3EventKey(const std::string& encoded) {
        std::string out;
        out.reserve(encoded.size());
        for (size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] == '+') out.push_back(' ');
            else if (encoded[i] == '%' && i + 2 < encoded.size()) {
                const auto hi = hexValue(encoded[i + 1]);
                const auto lo = hexValue(encoded[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                } else out.push_back(encoded[i]);
            } else out.push_back(encoded[i]);
        }
        return out;
    }

    void collectS3EventRecords(const nlohmann::json& payload, std::vector<nlohmann::json>& records) {
        if (!payload.is_object()) return;

        if (payload.contains("Records") && payload.at("Records").is_array()) {
            for (const auto& record : payload.at("Records")) {
                if (record.contains("s3")) records.push_back(record);
                else if (record.contains("body") && record.at("body").is_string()) {
                    try {
                        collectS3EventRecords(nlohmann::json::parse(record.at("body").get<std::string>()), records);
                    } catch (...) {}
                } else if (record.contains("Sns") && record.at("Sns").contains("Message") &&
                           record.at("Sns").at("Message").is_string()) {
                    try {
                        collectS3EventRecords(nlohmann::json::parse(record.at("Sns").at("Message").get<std::string>()), records);
                    } catch (...) {}
                }
            }
            return;
        }

        if (payload.contains("s3")) records.push_back(payload);
    }

    struct EventIngestResult {
        size_t records{};
        size_t upserts{};
        size_t deletes{};
        size_t skipped{};
    };

    bool startsWith(const std::string& value, const std::string_view prefix) {
        return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
    }

    EventIngestResult ingestS3EventFile(const uint32_t vaultId, const std::filesystem::path& filePath) {
        std::ifstream input(filePath);
        if (!input) throw std::runtime_error("unable to open S3 event JSON: " + filePath.string());

        const auto payload = nlohmann::json::parse(input);
        std::vector<nlohmann::json> records;
        collectS3EventRecords(payload, records);

        EventIngestResult result{.records = records.size()};

        for (const auto& record : records) {
            const auto eventName = record.value("eventName", std::string{});
            if (!record.contains("s3") || !record.at("s3").contains("object") ||
                !record.at("s3").at("object").contains("key") ||
                !record.at("s3").at("object").at("key").is_string()) {
                ++result.skipped;
                continue;
            }

            const auto key = decodeS3EventKey(record.at("s3").at("object").at("key").get<std::string>());
            if (key.empty() || remote_manifest::isVaulthallaManifestKey(key)) {
                ++result.skipped;
                continue;
            }

            const auto& object = record.at("s3").at("object");
            const auto sequencer = object.contains("sequencer") && object.at("sequencer").is_string()
                ? std::make_optional(object.at("sequencer").get<std::string>())
                : std::optional<std::string>{};
            const auto versionId = object.contains("versionId") && object.at("versionId").is_string()
                ? std::make_optional(object.at("versionId").get<std::string>())
                : std::optional<std::string>{};

            if (startsWith(eventName, "ObjectRemoved:")) {
                db::query::sync::RemoteObjectIndex::deleteEventKey(vaultId, key, sequencer);
                ++result.deletes;
                continue;
            }

            if (!startsWith(eventName, "ObjectCreated:")) {
                ++result.skipped;
                continue;
            }

            const auto size = object.contains("size") && object.at("size").is_number_integer() &&
                              object.at("size").get<int64_t>() >= 0
                ? static_cast<uint64_t>(object.at("size").get<int64_t>())
                : uint64_t{0};

            auto file = std::make_shared<vh::fs::model::File>(
                key,
                size,
                parseObjectTime(record.value("eventTime", std::string{})));
            if (object.contains("eTag") && object.at("eTag").is_string())
                file->remote_etag = object.at("eTag").get<std::string>();
            file->remote_version_id = versionId;
            file->remote_sequencer = sequencer;

            db::query::sync::RemoteObjectIndex::upsertEventFile(vaultId, file);
            ++result.upserts;
        }

        return result;
    }

    std::optional<uint64_t> parseBudgetValue(const std::string& raw) {
        const auto value = canonicalColumn(raw);
        if (value.empty() || value == "none" || value == "null" || value == "unlimited") return std::nullopt;
        return parseUint64(raw);
    }

    bool applyBudgetOption(
        const CommandCall& call,
        const std::string& option,
        std::optional<uint64_t>& budgetValue,
        std::string& error) {
        const auto value = optVal(call, option);
        if (!value) return true;
        const auto parsed = parseBudgetValue(*value);
        if (!parsed && !canonicalColumn(*value).empty() &&
            canonicalColumn(*value) != "none" &&
            canonicalColumn(*value) != "null" &&
            canonicalColumn(*value) != "unlimited") {
            error = "vault sync update: --" + option + " must be a non-negative integer or 'unlimited'";
            return false;
        }
        budgetValue = parsed;
        return true;
    }

    std::string remoteIndexSummaryString(
        const db::query::sync::RemoteIndexSummary& summary,
        const std::optional<std::chrono::seconds>& maxAge) {
        const auto optionalTime = [](const std::optional<std::time_t>& value) {
            return value ? timestampToString(*value) : std::string("unknown");
        };

        std::ostringstream out;
        out << "\n  Remote Index:\n"
            << "    Source: " << summary.source.value_or(summary.object_count > 0 ? "mixed" : "none") << "\n"
            << "    Indexed At: " << optionalTime(summary.indexed_at) << "\n"
            << "    Object Count: " << summary.object_count << "\n"
            << "    Freshness: " << (summary.isStale(maxAge) ? "stale" : "fresh") << "\n"
            << "    Manifest ETag: " << summary.manifest_etag.value_or("unknown") << "\n"
            << "    Manifest Generated At: " << optionalTime(summary.manifest_generated_at) << "\n"
            << "    Manifest Object Count: "
            << (summary.manifest_object_count ? std::to_string(*summary.manifest_object_count) : std::string("unknown"));
        return out.str();
    }

    std::string s3CostEstimateString(const S3CostEstimate& estimate) {
        std::ostringstream out;
        out << "  Estimated S3 requests:\n"
            << "    LIST: " << estimate.list_requests << "\n"
            << "    HEAD: " << estimate.head_requests << "\n"
            << "    GET: " << estimate.get_requests << "\n"
            << "    PUT: " << estimate.put_requests << "\n"
            << "    COPY: " << estimate.copy_requests << "\n"
            << "    DELETE: " << estimate.delete_requests << "\n"
            << "  Estimated traffic:\n"
            << "    Body download bytes: " << estimate.planned_body_download_bytes << "\n"
            << "    Upload bytes: " << estimate.planned_upload_bytes << "\n"
            << "    Cache/index-only objects: " << estimate.remote_index_objects << "\n"
            << "    Archive-tier body downloads skipped: " << estimate.archive_tier_downloads_skipped;
        return out.str();
    }

    uint64_t countPlanActions(const std::vector<Action>& plan, const ActionType type) {
        return static_cast<uint64_t>(std::ranges::count_if(plan, [type](const auto& action) {
            return action.type == type;
        }));
    }
}

static CommandResult handle_vault_sync(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync";

    const auto usage = resolveUsage({"vault", "sync"});
    validatePositionals(call, usage);

    const auto vLkp = resolveVault(call, call.positionals[0], usage, ERR);
    if (!vLkp || !vLkp.ptr) return invalid(vLkp.error);
    const auto vault = vLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncActionPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Trigger,
        .vault_id = vault->id
    })) return invalid("vault sync: you do not have permission to trigger a sync for this vault");

    runtime::Deps::get().syncController->runNow(vault->id);

    return ok("Vault sync initiated for '" + vault->name + "' (ID: " + std::to_string(vault->id) + ")");
}

static CommandResult handle_vault_sync_update(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync update";

    const auto usage = resolveUsage({"vault", "sync", "update"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncConfigPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Edit,
        .vault_id = engine->vault->id
    })) return invalid("vault sync update: you do not have permission to update this vault's sync configuration");

    if (const auto intervalOpt = optVal(call, "interval")) {
        try {
            engine->sync->interval = parseSyncInterval(*intervalOpt);
        } catch (const std::exception& e) {
            return invalid("vault sync update: " + std::string(e.what()));
        }
    }

    if (engine->vault->type == VaultType::Local) {
        if (const auto onSyncConflictOpt = optVal(call, usage->resolveOptional("on-sync-conflict")->option_tokens)) {
            const auto fsync = std::static_pointer_cast<LocalPolicy>(engine->sync);

            try {
                fsync->conflict_policy = fsConflictPolicyFromString(*onSyncConflictOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }
    } else if (engine->vault->type == VaultType::S3) {
        const auto rsync = std::static_pointer_cast<RemotePolicy>(engine->sync);

        if (const auto syncStrategyOpt = optVal(call, usage->resolveOptional("sync-strategy")->option_tokens)) {
            try {
                rsync->strategy = strategyFromString(*syncStrategyOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }

        if (const auto onSyncConflictOpt = optVal(call, usage->resolveOptional("on-sync-conflict")->option_tokens)) {
            try {
                rsync->conflict_policy = rsConflictPolicyFromString(*onSyncConflictOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }

        if (const auto presetOpt = optVal(call, "s3-budget-preset")) {
            try {
                rsync->s3_request_budget = s3RequestBudgetForPreset(s3BudgetPresetFromString(*presetOpt));
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }

        if (const auto indexAgeOpt = optVal(call, "max-remote-index-age")) {
            try {
                rsync->max_remote_index_age = remoteIndexAgeFromString(*indexAgeOpt);
            } catch (const std::exception& e) {
                return invalid("vault sync update: " + std::string(e.what()));
            }
        }

        std::string budgetError;
        if (!applyBudgetOption(call, "s3-budget-list", rsync->s3_request_budget.max_list_requests, budgetError) ||
            !applyBudgetOption(call, "s3-budget-head", rsync->s3_request_budget.max_head_requests, budgetError) ||
            !applyBudgetOption(call, "s3-budget-get", rsync->s3_request_budget.max_get_requests, budgetError) ||
            !applyBudgetOption(call, "s3-budget-put", rsync->s3_request_budget.max_put_requests, budgetError) ||
            !applyBudgetOption(call, "s3-budget-copy", rsync->s3_request_budget.max_copy_requests, budgetError) ||
            !applyBudgetOption(call, "s3-budget-delete", rsync->s3_request_budget.max_delete_requests, budgetError) ||
            !applyBudgetOption(call, "s3-budget-download-bytes", rsync->s3_request_budget.max_downloaded_bytes, budgetError))
            return invalid(budgetError);
    }

    engine->sync->interval = Policy::clampInterval(engine->sync->interval);
    engine->sync->rehash_config();
    db::query::vault::Vault::updateVaultSync(engine->sync, engine->vault->type);

    if (engine->vault->type == VaultType::Local) {
        const auto fsync = std::static_pointer_cast<LocalPolicy>(engine->sync);
        return ok("Successfully updated local vault sync configuration!\n" + to_string(fsync));
    }

    if (engine->vault->type == VaultType::S3) {
        const auto rsync = std::static_pointer_cast<RemotePolicy>(engine->sync);
        return ok("Successfully updated S3 vault sync configuration!\n" + to_string(rsync));
    }

    return invalid("vault sync update: invalid sync configuration");
}

static CommandResult handle_vault_sync_reconcile(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync reconcile";

    const auto usage = resolveUsage({"vault", "sync", "reconcile"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncActionPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Trigger,
        .vault_id = engine->vault->id
    })) return invalid("vault sync reconcile: you do not have permission to reconcile this vault");

    if (engine->vault->type != VaultType::S3)
        return invalid("vault sync reconcile: remote reconciliation is only available for S3 vaults");

    const auto cloud = std::static_pointer_cast<CloudEngine>(engine);
    const auto policy = cloud->remote_policy();
    const auto priorIndexedObjects = db::query::sync::RemoteObjectIndex::countForVault(engine->vault->id);
    const auto estimatedListRequests = priorIndexedObjects > 0 ? ((priorIndexedObjects + 999) / 1000) : uint64_t{0};

    if (!policy->s3_request_budget.max_list_requests &&
        !hasFlag(call, std::vector<std::string>{"allow-list-scan"})) {
        return invalid(
            "vault sync reconcile: full bucket LIST scans require --allow-list-scan or a configured --s3-budget-list.\n"
            "  Pre-run LIST estimate: " + (
                priorIndexedObjects > 0
                    ? std::to_string(estimatedListRequests) + " based on " + std::to_string(priorIndexedObjects) + " indexed objects"
                    : std::string("unknown; no prior remote index was available")));
    }

    try {
        const ScopedS3RequestBudget s3Budget(cloud, policy->s3_request_budget);

        const auto remoteMap = cloud->getGroupedFilesFromS3();
        std::vector<std::shared_ptr<vh::fs::model::File>> files;
        files.reserve(remoteMap.size());
        for (const auto& [_, file] : remoteMap) files.push_back(file);
        db::query::sync::RemoteObjectIndex::replaceFromListObjects(engine->vault->id, files);
        cloud->publishRemoteIndexManifest();

        const auto metrics = s3Budget.metrics();

        return ok(
            "Remote index reconciled for '" + engine->vault->name + "' (ID: " + std::to_string(engine->vault->id) + ")\n"
            "  Pre-run LIST estimate: " + (
                priorIndexedObjects > 0
                    ? std::to_string(estimatedListRequests) + " based on " + std::to_string(priorIndexedObjects) + " indexed objects"
                    : std::string("unknown; no prior remote index was available")) + "\n"
            "  Objects indexed: " + std::to_string(files.size()) + "\n"
            "  LIST requests: " + std::to_string(metrics.list_requests) + " (S3 returns up to 1,000 objects per request)\n"
            "  HEAD requests: " + std::to_string(metrics.head_requests) + "\n"
            "  GET requests: " + std::to_string(metrics.get_requests) + "\n"
            "  PUT requests: " + std::to_string(metrics.put_requests) + "\n"
            "  COPY requests: " + std::to_string(metrics.copy_requests) + "\n"
            "  DELETE requests: " + std::to_string(metrics.delete_requests) + "\n"
            "  Downloaded bytes: " + std::to_string(metrics.downloaded_bytes));
    } catch (const std::exception& e) {
        return invalid("vault sync reconcile: " + std::string(e.what()));
    }
}

static CommandResult handle_vault_sync_inventory(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync inventory";

    const auto usage = resolveUsage({"vault", "sync", "inventory"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncActionPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Trigger,
        .vault_id = engine->vault->id
    })) return invalid("vault sync inventory: you do not have permission to import inventory for this vault");

    if (engine->vault->type != VaultType::S3)
        return invalid("vault sync inventory: inventory import is only available for S3 vaults");

    const auto fileOpt = optVal(call, usage->resolveRequired("file")->option_tokens);
    if (!fileOpt || fileOpt->empty()) return invalid("vault sync inventory: --file is required");

    const auto inventoryPath = std::filesystem::path(*fileOpt);
    if (!std::filesystem::exists(inventoryPath))
        return invalid("vault sync inventory: file does not exist: " + inventoryPath.string());

    const auto cloud = std::static_pointer_cast<CloudEngine>(engine);
    const auto policy = cloud->remote_policy();

    try {
        size_t skippedRows = 0;
        const auto schemaOpt = optVal(call, usage->resolveOptional("schema")->option_tokens);
        const auto files = parseInventoryCsv(
            inventoryPath,
            schemaOpt,
            hasFlag(call, std::vector<std::string>{"has-header", "header"}),
            skippedRows);

        db::query::sync::RemoteObjectIndex::replace(engine->vault->id, files, "inventory");

        const ScopedS3RequestBudget s3Budget(cloud, policy->s3_request_budget);
        cloud->publishRemoteIndexManifest();
        const auto metrics = s3Budget.metrics();

        return ok(
            "S3 inventory imported for '" + engine->vault->name + "' (ID: " + std::to_string(engine->vault->id) + ")\n"
            "  Objects indexed: " + std::to_string(files.size()) + "\n"
            "  Rows skipped: " + std::to_string(skippedRows) + "\n"
            "  Manifest PUT requests: " + std::to_string(metrics.put_requests) + "\n"
            "  Manifest HEAD requests: " + std::to_string(metrics.head_requests));
    } catch (const std::exception& e) {
        return invalid("vault sync inventory: " + std::string(e.what()));
    }
}

static CommandResult handle_vault_sync_events(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync events";

    const auto usage = resolveUsage({"vault", "sync", "events"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncActionPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::Trigger,
        .vault_id = engine->vault->id
    })) return invalid("vault sync events: you do not have permission to ingest event notifications for this vault");

    if (engine->vault->type != VaultType::S3)
        return invalid("vault sync events: S3 event ingestion is only available for S3 vaults");

    const auto fileOpt = optVal(call, usage->resolveRequired("file")->option_tokens);
    if (!fileOpt || fileOpt->empty()) return invalid("vault sync events: --file is required");

    const auto eventPath = std::filesystem::path(*fileOpt);
    if (!std::filesystem::exists(eventPath))
        return invalid("vault sync events: file does not exist: " + eventPath.string());

    const auto cloud = std::static_pointer_cast<CloudEngine>(engine);
    const auto policy = cloud->remote_policy();

    try {
        const auto result = ingestS3EventFile(engine->vault->id, eventPath);
        if (result.records == 0) return invalid("vault sync events: no S3 event records found");

        const ScopedS3RequestBudget s3Budget(cloud, policy->s3_request_budget);
        if (result.upserts > 0 || result.deletes > 0) cloud->publishRemoteIndexManifest();
        const auto metrics = s3Budget.metrics();

        return ok(
            "S3 event notifications ingested for '" + engine->vault->name + "' (ID: " + std::to_string(engine->vault->id) + ")\n"
            "  Records read: " + std::to_string(result.records) + "\n"
            "  Objects upserted: " + std::to_string(result.upserts) + "\n"
            "  Objects deleted: " + std::to_string(result.deletes) + "\n"
            "  Records skipped: " + std::to_string(result.skipped) + "\n"
            "  Manifest PUT requests: " + std::to_string(metrics.put_requests) + "\n"
            "  Manifest HEAD requests: " + std::to_string(metrics.head_requests));
    } catch (const std::exception& e) {
        return invalid("vault sync events: " + std::string(e.what()));
    }
}

static CommandResult handle_vault_sync_info(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync info";

    const auto usage = resolveUsage({"vault", "sync", "info"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncConfigPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::View,
        .vault_id = engine->vault->id
    })) return invalid("vault sync info: you do not have permission to view the sync configuration for this vault");

    if (!engine->sync) return invalid("vault sync info: vault does not have a sync configuration");

    if (engine->vault->type == VaultType::S3) {
        const auto rsync = std::static_pointer_cast<RemotePolicy>(engine->sync);
        const auto summary = db::query::sync::RemoteObjectIndex::summaryForVault(engine->vault->id);
        return ok(to_string(rsync) + remoteIndexSummaryString(summary, rsync->max_remote_index_age));
    }

    return ok(to_string(engine->sync));
}

static CommandResult handle_vault_sync_dry_run(const CommandCall& call) {
    constexpr const auto* ERR = "vault sync dry-run";

    const auto usage = resolveUsage({"vault", "sync", "dry-run"});
    validatePositionals(call, usage);

    const auto eLkp = resolveEngine(call, call.positionals[0], usage, ERR);
    if (!eLkp || !eLkp.ptr) return invalid(eLkp.error);
    const auto engine = eLkp.ptr;

    using Perm = rbac::permission::vault::sync::SyncConfigPermissions;
    if (!rbac::resolver::Vault::has<Perm>({
        .user = call.user,
        .permission = Perm::View,
        .vault_id = engine->vault->id
    })) return invalid("vault sync dry-run: you do not have permission to view this vault's sync plan");

    if (engine->vault->type != VaultType::S3)
        return invalid("vault sync dry-run: S3 cost dry-run is only available for S3 vaults");

    const auto cloud = std::static_pointer_cast<CloudEngine>(engine);
    const auto policy = cloud->remote_policy();

    try {
        const ScopedS3RequestBudget s3Budget(cloud, policy->s3_request_budget);
        const bool manifestRefreshed = cloud->refreshRemoteIndexFromManifestIfChanged();
        const auto summary = db::query::sync::RemoteObjectIndex::summaryForVault(engine->vault->id);

        if (!manifestRefreshed && !summary.indexed_at)
            return invalid("vault sync dry-run: no remote index is available; run reconcile, inventory import, or event ingestion first");

        if (!manifestRefreshed && summary.isStale(policy->max_remote_index_age))
            return invalid("vault sync dry-run: remote index is stale and manifest refresh failed");

        auto ctx = std::make_shared<sync::Cloud>(cloud);
        ctx->event = std::make_shared<Event>();
        ctx->event->vault_id = engine->vault->id;
        ctx->localFiles = db::query::fs::File::listFilesInDir(engine->vault->id);
        ctx->localMap = vh::fs::model::groupEntriesByPath(ctx->localFiles);
        ctx->s3Files = db::query::sync::RemoteObjectIndex::listFilesForVault(engine->vault->id);
        ctx->s3Map = vh::fs::model::groupEntriesByPath(ctx->s3Files);

        S3CostEstimate planningNotes;
        const auto plan = vh::sync::Planner::build(ctx, policy, &planningNotes);
        auto estimate = vh::sync::Planner::estimateS3Cost(plan);
        estimate.archive_tier_downloads_skipped = planningNotes.archive_tier_downloads_skipped;

        const auto metrics = s3Budget.metrics();
        std::ostringstream out;
        out << "S3 sync dry-run for '" << engine->vault->name << "' (ID: " << engine->vault->id << ")\n"
            << "  Plan actions:\n"
            << "    Upload: " << countPlanActions(plan, ActionType::Upload) << "\n"
            << "    Download: " << countPlanActions(plan, ActionType::Download) << "\n"
            << "    Index remote-only: " << countPlanActions(plan, ActionType::IndexRemoteOnly) << "\n"
            << "    Delete remote: " << countPlanActions(plan, ActionType::DeleteRemote) << "\n"
            << "    Delete local: " << countPlanActions(plan, ActionType::DeleteLocal) << "\n"
            << s3CostEstimateString(estimate) << "\n"
            << "  Planning S3 requests used:\n"
            << "    HEAD: " << metrics.head_requests << "\n"
            << "    GET: " << metrics.get_requests << "\n"
            << "    LIST: " << metrics.list_requests << "\n"
            << remoteIndexSummaryString(summary, policy->max_remote_index_age);

        return ok(out.str());
    } catch (const std::exception& e) {
        return invalid("vault sync dry-run: " + std::string(e.what()));
    }
}

static bool isVaultSyncMatch(const std::string& cmd, const std::string_view input) {
    return isCommandMatch({"vault", "sync", cmd}, input);
}

CommandResult commands::vault::handle_sync(const CommandCall& call) {
    const auto [arg, subcall] = descend(call);
    if (call.positionals.size() == 1) return handle_vault_sync(call);
    if (isVaultSyncMatch("update", arg)) return handle_vault_sync_update(subcall);
    if (isVaultSyncMatch("reconcile", arg)) return handle_vault_sync_reconcile(subcall);
    if (isVaultSyncMatch("inventory", arg)) return handle_vault_sync_inventory(subcall);
    if (isVaultSyncMatch("events", arg)) return handle_vault_sync_events(subcall);
    if (isVaultSyncMatch("info", arg)) return handle_vault_sync_info(subcall);
    if (isVaultSyncMatch("dry-run", arg) || isVaultSyncMatch("dryrun", arg) || isVaultSyncMatch("plan", arg))
        return handle_vault_sync_dry_run(subcall);

    return invalid(call.constructFullArgs(), "vault sync: unknown subcommand: '" + std::string(arg) + "'");
}
