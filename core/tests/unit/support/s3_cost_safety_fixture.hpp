#pragma once

// Shared fixtures for the S3CostSafetyTest suite (split across test_s3_cost_safety_*.cpp so no single
// translation unit dominates the per-file build). Helpers are `inline` in a named namespace: one definition
// program-wide, no unused-function warnings in files that use only some of them, and no collisions with other
// test files' anonymous-namespace helpers when unity builds put them in the same chunk.

#include "db/query/rbac/role/Admin.hpp"
#include "auth/Manager.hpp"
#include "db/encoding/interval.hpp"
#include "db/Transactions.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/s3/Gateway.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/vault/Vault.hpp"
#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "crypto/id/Generator.hpp"
#include "fs/Filesystem.hpp"
#include "fs/model/Path.hpp"
#include "fs/model/File.hpp"
#include "fs/model/file/Trashed.hpp"
#include "identities/User.hpp"
#include "protocols/s3/CredentialManager.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/s3/Router.hpp"
#include "protocols/s3/SigV4.hpp"
#include "protocols/shell/commands/all.hpp"
#include "protocols/shell/commands/vault.hpp"
#include "protocols/shell/Router.hpp"
#include "protocols/ws/handler/Pricing.hpp"
#include "protocols/ws/handler/S3Gateway.hpp"
#include "protocols/ws/handler/vault/Vaults.hpp"
#include "protocols/ws/Router.hpp"
#include "protocols/ws/Session.hpp"
#include "rbac/role/Admin.hpp"
#include "rbac/role/Vault.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/Manager.hpp"
#include "storage/ScopedS3RequestBudget.hpp"
#include "storage/ScopedS3RequestUsageCapture.hpp"
#include "storage/s3/Controller.hpp"
#include "storage/s3/curl/helpers.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "sync/Cloud.hpp"
#include "sync/Planner.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/LocalPolicy.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemoteManifest.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/ScopedOp.hpp"
#include "sync/tasks/Delete.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/APIKeyManager.hpp"
#include "vault/EncryptionManager.hpp"
#include "UsageManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <chrono>
#include <algorithm>
#include <barrier>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <future>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vh::test::s3_cost_safety {

namespace http = boost::beast::http;

struct S3CostConfigRestore {
    vh::config::Config previous;

    explicit S3CostConfigRestore(vh::config::Config cfg)
        : previous(std::move(cfg)) {}

    ~S3CostConfigRestore() {
        vh::config::Registry::set(previous);
    }
};

inline std::shared_ptr<vh::vault::model::APIKey> dummyApiKey() {
    return std::make_shared<vh::vault::model::APIKey>(
        1,
        "unit",
        vh::vault::model::S3Provider::AWS,
        "ABCDEFGHIJKLMNOPQRST",
        "ABCDEFGHIJKLMNOPQRSTABCDEFGHIJKLMNOPQRST",
        "us-east-1",
        "https://s3.example.com");
}

class CountingS3Controller final : public vh::storage::s3::Controller {
public:
    int head_object_calls = 0;
    int download_to_buffer_calls = 0;
    int upload_object_with_metadata_calls = 0;
    int upload_buffer_with_metadata_calls = 0;
    int upload_buffer_conditional_calls = 0;
    int delete_object_calls = 0;
    int list_objects_calls = 0;
    bool upload_buffer_with_metadata_failure = false;
    bool delete_object_not_found = false;
    bool delete_object_failure = false;
    std::filesystem::path last_uploaded_key;
    std::size_t last_uploaded_buffer_size = 0;
    std::unordered_map<std::string, std::string> last_metadata;
    std::map<std::string, std::string> last_system_headers;
    std::optional<std::unordered_map<std::string, std::string>> head_response;
    std::vector<uint8_t> download_payload;
    std::deque<std::vector<uint8_t>> download_payloads;
    std::vector<std::filesystem::path> deleted_keys;
    std::u8string list_objects_xml = u8"<ListBucketResult></ListBucketResult>";

    CountingS3Controller()
        : Controller(dummyApiKey(), "unit-bucket") {}

    void uploadObjectWithMetadata(
        const std::filesystem::path&,
        const std::filesystem::path&,
        const std::unordered_map<std::string, std::string>& metadata) const override {
        vh::storage::s3::RequestOptions options;
        options.metadata = metadata;
        uploadObjectWithMetadata(std::filesystem::path{}, std::filesystem::path{}, options);
    }

    void uploadObjectWithMetadata(
        const std::filesystem::path&,
        const std::filesystem::path&,
        const vh::storage::s3::RequestOptions& options) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->upload_object_with_metadata_calls;
        self->last_metadata = options.metadata;
        self->last_system_headers = options.system_headers;
    }

    void uploadLargeObject(
        const std::filesystem::path&,
        const std::filesystem::path&,
        uintmax_t,
        const std::unordered_map<std::string, std::string>& metadata) const override {
        vh::storage::s3::RequestOptions options;
        options.metadata = metadata;
        uploadLargeObject(std::filesystem::path{}, std::filesystem::path{}, 0, options);
    }

    void uploadLargeObject(
        const std::filesystem::path&,
        const std::filesystem::path&,
        uintmax_t,
        const vh::storage::s3::RequestOptions& options) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        self->last_metadata = options.metadata;
        self->last_system_headers = options.system_headers;
    }

    void uploadLargeObject(
        const std::filesystem::path&,
        const std::vector<uint8_t>&,
        uintmax_t,
        const vh::storage::s3::RequestOptions& options) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        self->last_metadata = options.metadata;
        self->last_system_headers = options.system_headers;
    }

    void downloadToBuffer(const std::filesystem::path&, std::vector<uint8_t>& out) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        self->recordRequest(RequestKind::Get);
        ++self->download_to_buffer_calls;
        if (!self->download_payloads.empty()) {
            out = std::move(self->download_payloads.front());
            self->download_payloads.pop_front();
            self->recordRequest(RequestKind::DownloadBytes, out.size());
            return;
        }
        out = self->download_payload;
        self->recordRequest(RequestKind::DownloadBytes, out.size());
    }

    void uploadBufferWithMetadata(
        const std::filesystem::path& key,
        const std::vector<uint8_t>& buffer,
        const std::unordered_map<std::string, std::string>& metadata) const override {
        vh::storage::s3::RequestOptions options;
        options.metadata = metadata;
        uploadBufferWithMetadata(key, buffer, options);
    }

    void uploadBufferWithMetadata(
        const std::filesystem::path& key,
        const std::vector<uint8_t>& buffer,
        const vh::storage::s3::RequestOptions& options) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->upload_buffer_with_metadata_calls;
        self->last_uploaded_key = key;
        self->last_uploaded_buffer_size = buffer.size();
        self->last_metadata = options.metadata;
        self->last_system_headers = options.system_headers;
        self->recordRequest(RequestKind::Put);
        self->recordUploadBytes(buffer.size());
        if (self->upload_buffer_with_metadata_failure)
            throw std::runtime_error("upload failed after upstream PUT attempt");
    }

    void uploadBufferWithMetadataConditional(
        const std::filesystem::path&,
        const std::vector<uint8_t>&,
        const std::unordered_map<std::string, std::string>&,
        const std::optional<std::string>&,
        const std::optional<std::string>&) const override {
        uploadBufferWithMetadataConditional(
            std::filesystem::path{},
            std::vector<uint8_t>{},
            vh::storage::s3::RequestOptions{},
            std::nullopt,
            std::nullopt);
    }

    void uploadBufferWithMetadataConditional(
        const std::filesystem::path&,
        const std::vector<uint8_t>&,
        const vh::storage::s3::RequestOptions& options,
        const std::optional<std::string>&,
        const std::optional<std::string>&) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->upload_buffer_conditional_calls;
        self->last_metadata = options.metadata;
        self->last_system_headers = options.system_headers;
        self->recordRequest(RequestKind::Put);
    }

    std::optional<std::unordered_map<std::string, std::string>> getHeadObject(
        const std::filesystem::path&) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->head_object_calls;
        self->recordRequest(RequestKind::Head);
        return head_response;
    }

    void deleteObject(const std::filesystem::path& key) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->delete_object_calls;
        self->deleted_keys.push_back(key);
        self->recordRequest(RequestKind::Delete);
        if (self->delete_object_not_found)
            throw vh::storage::s3::ObjectNotFound("NoSuchKey");
        if (self->delete_object_failure)
            throw std::runtime_error("delete failed");
    }

    std::u8string listObjects(const std::filesystem::path&) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->list_objects_calls;
        self->recordRequest(RequestKind::List);
        return self->list_objects_xml;
    }
};

class ManifestRaceS3Controller final : public vh::storage::s3::Controller {
public:
    mutable std::vector<std::optional<std::unordered_map<std::string, std::string>>> head_responses;
    mutable std::size_t head_index = 0;
    mutable std::vector<int> conditional_failures;
    mutable std::size_t failure_index = 0;
    mutable std::vector<std::optional<std::string>> if_match_values;
    mutable std::vector<std::optional<std::string>> if_none_match_values;
    std::string manifest;

    explicit ManifestRaceS3Controller(std::string manifestBody = {})
        : Controller(dummyApiKey(), "unit-bucket"),
          manifest(std::move(manifestBody)) {}

    void uploadBufferWithMetadataConditional(
        const std::filesystem::path&,
        const std::vector<uint8_t>&,
        const std::unordered_map<std::string, std::string>&,
        const std::optional<std::string>& ifMatch,
        const std::optional<std::string>& ifNoneMatch) const override {
        uploadBufferWithMetadataConditional(
            std::filesystem::path{},
            std::vector<uint8_t>{},
            vh::storage::s3::RequestOptions{},
            ifMatch,
            ifNoneMatch);
    }

    void uploadBufferWithMetadataConditional(
        const std::filesystem::path&,
        const std::vector<uint8_t>&,
        const vh::storage::s3::RequestOptions&,
        const std::optional<std::string>& ifMatch,
        const std::optional<std::string>& ifNoneMatch) const override {
        if_match_values.push_back(ifMatch);
        if_none_match_values.push_back(ifNoneMatch);
        if (failure_index < conditional_failures.size()) {
            const auto code = conditional_failures[failure_index++];
            throw vh::storage::s3::ConditionalRequestFailed(
                "Conditional S3 PUT failed for manifest (HTTP " + std::to_string(code) + ")");
        }
    }

    void downloadToBuffer(const std::filesystem::path&, std::vector<uint8_t>& outBuffer) const override {
        outBuffer.assign(manifest.begin(), manifest.end());
    }

    std::optional<std::unordered_map<std::string, std::string>> getHeadObject(
        const std::filesystem::path&) const override {
        if (head_index < head_responses.size()) return head_responses[head_index++];
        return std::nullopt;
    }
};

class BudgetProbeS3Controller final : public vh::storage::s3::Controller {
public:
    using RequestKind = vh::storage::s3::Controller::RequestKind;

    int set_budget_calls = 0;
    int clear_budget_calls = 0;
    int reset_metrics_calls = 0;

    BudgetProbeS3Controller()
        : Controller(dummyApiKey(), "unit-bucket") {}

    void setRequestBudget(const vh::storage::s3::S3RequestBudget& budget) const override {
        ++const_cast<BudgetProbeS3Controller*>(this)->set_budget_calls;
        Controller::setRequestBudget(budget);
    }

    void clearRequestBudget() const override {
        ++const_cast<BudgetProbeS3Controller*>(this)->clear_budget_calls;
        Controller::clearRequestBudget();
    }

    void resetRequestMetrics() const override {
        ++const_cast<BudgetProbeS3Controller*>(this)->reset_metrics_calls;
        Controller::resetRequestMetrics();
    }

    void count(RequestKind kind, uint64_t amount = 1) const {
        recordRequest(kind, amount);
    }

    void simulateMultipartPutCounts(int parts) const {
        count(RequestKind::Put); // initiate
        for (int i = 0; i < parts; ++i) count(RequestKind::Put);
        count(RequestKind::Put); // complete
    }
};

inline bool hasDbEnv() {
    return std::getenv("VH_TEST_DB_USER") &&
           std::getenv("VH_TEST_DB_PASS") &&
           std::getenv("VH_TEST_DB_HOST") &&
           std::getenv("VH_TEST_DB_PORT") &&
           std::getenv("VH_TEST_DB_NAME");
}

inline std::string uniqueSuffix(const std::string& label) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return label + "_" + std::to_string(ticks);
}

inline std::string uniqueBucketName(const std::string& label) {
    auto out = "gw-" + uniqueSuffix(label);
    for (auto& c : out) {
        if (c == '_') c = '-';
    }
    if (out.size() > 63) out.resize(63);
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() < 3) out = "gw0";
    return out;
}

inline std::string gatewayAmzNow() {
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return out.str();
}

inline std::string gatewayScopeDate(const std::string& amzDate) {
    return amzDate.substr(0, 8);
}

inline void signGatewayRouteRequest(
    vh::protocols::s3::Router::Request& request,
    const std::string& accessKey,
    const std::string& secretKey) {
    using namespace vh::protocols::s3::sigv4;

    const auto bodyHash = sha256Hex(request.body());
    const auto amzDate = gatewayAmzNow();
    request.set("x-amz-content-sha256", bodyHash);
    request.set("x-amz-date", amzDate);

    VerificationInput input = inputFromRequest(request, request.body());
    ParsedAuth auth{
        .credential = {
            .access_key = accessKey,
            .date = gatewayScopeDate(amzDate),
            .region = "us-east-1",
            .service = "s3"
        },
        .signed_headers = "host;x-amz-content-sha256;x-amz-date",
        .signature = {},
        .amz_date = amzDate,
        .payload_hash = bodyHash
    };
    auth.signature = signatureFor(input, auth, secretKey);
    request.set(
        http::field::authorization,
        "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + auth.credential.date + "/us-east-1/s3/aws4_request, "
        "SignedHeaders=" + auth.signed_headers + ", Signature=" + auth.signature);
}

inline uint32_t ensureS3CostAdminRole(pqxx::work& txn, const vh::rbac::role::Admin& role) {
    return txn.exec(
        R"SQL(
            INSERT INTO admin_role (
                name,
                description,
                identity_permissions,
                audit_permissions,
                settings_permissions,
                roles_permissions,
                vaults_permissions,
                keys_permissions,
                s3_gateway_permissions
            )
            VALUES ($1, $2, $3::bit(32), $4::bit(8), $5::bit(64), $6::bit(16), $7::bit(32), $8::bit(32), $9::bit(8))
            ON CONFLICT (name) DO UPDATE SET
                description = EXCLUDED.description,
                identity_permissions = EXCLUDED.identity_permissions,
                audit_permissions = EXCLUDED.audit_permissions,
                settings_permissions = EXCLUDED.settings_permissions,
                roles_permissions = EXCLUDED.roles_permissions,
                vaults_permissions = EXCLUDED.vaults_permissions,
                keys_permissions = EXCLUDED.keys_permissions,
                s3_gateway_permissions = EXCLUDED.s3_gateway_permissions
            RETURNING id
        )SQL",
        pqxx::params{
            role.name,
            role.description,
            role.identities.toBitString(),
            role.audits.toBitString(),
            role.settings.toBitString(),
            role.roles.toBitString(),
            role.vaults.toBitString(),
            role.keys.toBitString(),
            role.s3Gateway.toBitString()
        }).one_field().as<uint32_t>();
}

inline uint32_t ensureS3CostUnprivilegedAdminRole(pqxx::work& txn) {
    return ensureS3CostAdminRole(txn, vh::rbac::role::Admin::None());
}

inline uint32_t ensureS3CostSuperAdminRole(pqxx::work& txn) {
    return ensureS3CostAdminRole(txn, vh::rbac::role::Admin::SuperAdmin());
}

inline uint32_t ensureS3CostVaultAdminRole(pqxx::work& txn) {
    return ensureS3CostAdminRole(txn, vh::rbac::role::Admin::VaultAdmin());
}

inline vh::rbac::role::Admin s3GatewayBudgetManagerRole(const std::optional<std::uint32_t> userId = std::nullopt) {
    auto s3Gateway = vh::rbac::permission::admin::S3Gateway::None();
    s3Gateway.grant(vh::rbac::permission::admin::S3GatewayPermissions::ManageBudgets);
    return vh::rbac::role::Admin::Custom(
        "s3_gateway_budget_manager",
        "Test role that may manage S3 gateway budgets only.",
        vh::rbac::permission::admin::Identities::None(),
        vh::rbac::permission::admin::Vaults::None(),
        vh::rbac::permission::admin::Audits::None(),
        vh::rbac::permission::admin::Settings::None(),
        vh::rbac::permission::admin::Roles::None(),
        vh::rbac::permission::admin::Keys::None(),
        s3Gateway,
        userId);
}

inline uint32_t insertS3CostHydratableTestUser(pqxx::work& txn, const std::string& name, const std::string& email) {
    const auto userId = txn.exec(
        "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
        pqxx::params{name, email, "hash"}
    ).one_field().as<uint32_t>();
    const auto roleId = ensureS3CostUnprivilegedAdminRole(txn);
    txn.exec(
        "INSERT INTO admin_role_assignments (user_id, role_id) VALUES ($1, $2)",
        pqxx::params{userId, roleId});
    return userId;
}

inline uint32_t seedS3CostUserForDbTest(const std::string& suffix, const std::string& label) {
    return vh::db::Transactions::exec("S3CostSafetyTest::seedUser", [&](pqxx::work& txn) {
        return insertS3CostHydratableTestUser(
            txn,
            "s3_cost_safety_" + label + "_" + suffix,
            "s3-cost-safety-" + label + "-" + suffix + "@vaulthalla.test");
    });
}

inline uint32_t seedS3CostSuperAdminUserForDbTest(const std::string& suffix, const std::string& label) {
    return vh::db::Transactions::exec("S3CostSafetyTest::seedSuperAdminUser", [&](pqxx::work& txn) {
        const auto userId = txn.exec(
            "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
            pqxx::params{
                "s3_cost_safety_admin_" + label + "_" + suffix,
                "s3-cost-safety-admin-" + label + "-" + suffix + "@vaulthalla.test",
                "hash"
            }).one_field().as<uint32_t>();
        const auto roleId = ensureS3CostSuperAdminRole(txn);
        txn.exec(
            "INSERT INTO admin_role_assignments (user_id, role_id) VALUES ($1, $2)",
            pqxx::params{userId, roleId});
        return userId;
    });
}

inline uint32_t seedS3CostVaultAdminUserForDbTest(const std::string& suffix, const std::string& label) {
    return vh::db::Transactions::exec("S3CostSafetyTest::seedVaultAdminUser", [&](pqxx::work& txn) {
        const auto userId = txn.exec(
            "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
            pqxx::params{
                "s3_cost_safety_vault_admin_" + label + "_" + suffix,
                "s3-cost-safety-vault-admin-" + label + "-" + suffix + "@vaulthalla.test",
                "hash"
            }).one_field().as<uint32_t>();
        const auto roleId = ensureS3CostVaultAdminRole(txn);
        txn.exec(
            "INSERT INTO admin_role_assignments (user_id, role_id) VALUES ($1, $2)",
            pqxx::params{userId, roleId});
        return userId;
    });
}

inline void deleteIncompleteS3CostVaultFixturesForDbTest() {
    vh::db::Transactions::exec("S3CostSafetyTest::deleteIncompleteS3VaultFixtures", [&](pqxx::work& txn) {
        txn.exec(
            "DELETE FROM vault v "
            "WHERE v.type = 's3' "
            "AND ("
            "    NOT EXISTS (SELECT 1 FROM s3 WHERE s3.vault_id = v.id) "
            " OR NOT EXISTS ("
            "        SELECT 1 "
            "        FROM sync sy "
            "        JOIN rsync rs ON rs.sync_id = sy.id "
            "        WHERE sy.vault_id = v.id"
            "    )"
            ")");
    });
}

inline void clearS3PriceBudgetStateForDbTest() {
    vh::db::Transactions::exec("S3CostSafetyTest::clearPriceBudgetState", [](pqxx::work& txn) {
        txn.exec("DELETE FROM s3_price_budget_alert_state");
        txn.exec("DELETE FROM s3_price_budget_override");
        txn.exec("DELETE FROM operator_notification WHERE type LIKE 'budget.%' OR type LIKE 's3.%'");
        txn.exec("DELETE FROM s3_price_budget_ledger");
        txn.exec("DELETE FROM s3_price_budget_policy");
    });
}

inline void ensureDbReady() {
    static bool initialized = false;
    if (!initialized) {
        vh::db::Transactions::init();
        vh::db::seed::nuke_and_recreate_schema_public();
        vh::db::Transactions::dbPool_->initPreparedStatements();
        initialized = true;
    }
    clearS3PriceBudgetStateForDbTest();
}

inline void ensureWritableTestPathRoots() {
    static bool initialized = false;
    if (initialized) return;

    const auto root = std::filesystem::temp_directory_path() / uniqueSuffix("vh_s3_cost_safety_paths");
    std::filesystem::remove_all(root);
    vh::paths::backingPath = root / "backing";
    vh::paths::mountPath = root / "mount";
    std::filesystem::create_directories(vh::paths::backingPath);
    std::filesystem::create_directories(vh::paths::mountPath);
    initialized = true;
}

inline nlohmann::json gatewayRouteMeter(
    const std::string& meterKey,
    const std::string& rate,
    const std::string& rateUnit) {
    return {
        {"meter_key", meterKey},
        {"meter_type", "request"},
        {"unit", "operation"},
        {"billing_unit", rateUnit},
        {"rounding_rule", "none"},
        {"free_tier_scope", "none"},
        {"rules", nlohmann::json::object()},
        {"tiers", nlohmann::json::array({{
            {"rate", rate},
            {"rate_unit", rateUnit},
            {"tier_start", "0"}
        }})}
    };
}

inline void seedAwsGatewayRoutePriceCatalog() {
    ensureWritableTestPathRoots();

    const auto profile = nlohmann::json{
        {"schema_version", "1.0"},
        {"kind", "vaulthalla.rating_profile"},
        {"profile_id", "aws-s3/us-east-1/standard"},
        {"catalog_version", "gateway-route-fixture"},
        {"provider", {{"id", "aws-s3"}, {"display_name", "AWS S3"}, {"api_family", "s3-compatible"}}},
        {"scope", {{"region", "us-east-1"}, {"storage_class", "standard"}, {"currency", "USD"}}},
        {"confidence", {{"level", "high"}, {"score", "0.90"}, {"reasons", nlohmann::json::array()}, {"unknowns", nlohmann::json::array()}}},
        {"operation_map", {
            {"PutObject", {{"meter_key", "request_write"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"UploadPart", {{"meter_key", "request_write"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"CompleteMultipartUpload", {{"meter_key", "request_write"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"CopyObject", {{"meter_key", "request_write"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"GetObject", {{"meter_key", "request_read"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"HeadObject", {{"meter_key", "request_read"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"DeleteObject", {{"meter_key", "free_operations"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"DeleteObjects", {{"meter_key", "free_operations"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}},
            {"ListObjectsV2", {{"meter_key", "request_write"}, {"multiplier", "1"}, {"rules", nlohmann::json::object()}}}
        }},
        {"meters", {
            {"request_write", gatewayRouteMeter("request_write", "0.005", "request_1000")},
            {"request_read", gatewayRouteMeter("request_read", "0.0004", "request_1000")},
            {"free_operations", gatewayRouteMeter("free_operations", "0", "operation")}
        }},
        {"storage_rules", nlohmann::json::array()},
        {"provenance", nlohmann::json::object()},
        {"integrity", {{"content_sha256", ""}, {"signature_alg", "Ed25519"}, {"signature_ref", ""}}}
    };
    const auto profileBody = profile.dump();
    const std::string href = "profiles/aws-s3/us-east-1/standard.json";
    const auto manifest = nlohmann::json{
        {"schema_version", "1.0"},
        {"kind", "vaulthalla.price_manifest"},
        {"catalog_version", "gateway-route-fixture"},
        {"generated_at", "2026-05-26T20:00:10Z"},
        {"profile_count", 1},
        {"profiles", nlohmann::json::array({{
            {"profile_id", "aws-s3/us-east-1/standard"},
            {"provider", "aws-s3"},
            {"region", "us-east-1"},
            {"storage_class", "standard"},
            {"currency", "USD"},
            {"href", href},
            {"sha256", vh::storage::s3::curl::sha256Hex(profileBody)},
            {"signature", href + ".sig"},
            {"confidence_level", "high"},
            {"effective_at", "2026-05-26T20:00:10Z"}
        }})},
        {"full_catalog", nlohmann::json::object()},
        {"schemas", nlohmann::json::object()},
        {"integrity", {{"content_sha256", ""}, {"signature_alg", "Ed25519"}, {"signature_ref", "manifest.json.sig"}}}
    };

    const auto cacheRoot = vh::paths::getBackingPath() / "price-cache" / "artifacts";
    std::filesystem::create_directories(cacheRoot / "profiles/aws-s3/us-east-1");
    std::ofstream(cacheRoot / href, std::ios::binary | std::ios::trunc) << profileBody;
    std::ofstream(cacheRoot / "manifest.json", std::ios::binary | std::ios::trunc) << manifest.dump();
}

inline void ensureSeededRuntimeReady() {
    static bool seeded = false;
    ensureWritableTestPathRoots();
    ensureDbReady();
    if (!seeded) {
        deleteIncompleteS3CostVaultFixturesForDbTest();
        vh::seed::seed_database();
        vh::runtime::Deps::init();
        vh::fs::Filesystem::init(vh::runtime::Deps::get().storageManager);
        seeded = true;
    }
}

inline uint32_t seedS3VaultForDbTest(const std::string& suffix) {
    return vh::db::Transactions::exec("S3CostSafetyTest::seedS3Vault", [&](pqxx::work& txn) {
        const auto userId = insertS3CostHydratableTestUser(
            txn,
            "s3_cost_safety_user_" + suffix,
            "s3-cost-safety-" + suffix + "@vaulthalla.test");

        return txn.exec(
            "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
            pqxx::params{
                "s3",
                "S3 Cost Safety " + suffix,
                userId,
                "0123456789ABCDEFGHJKMNPQRSTVWXYZ",
                ""
            }).one_field().as<uint32_t>();
    });
}

inline vh::storage::s3::pricing::PriceEstimateReport budgetEstimateForDbTest(
    const std::string& cost,
    const bool verified = true,
    const bool stale = false) {
    vh::storage::s3::pricing::PriceEstimateReport report;
    report.available = true;
    report.supported = true;
    report.stale = stale;
    report.estimated_cost = cost;
    report.currency = "USD";
    report.price_profile_id = "aws-s3/us-east-1/standard";
    report.catalog_version = "test";
    report.catalog_source = "test";
    report.catalog_verified = verified;
    report.catalog_age_seconds = 60;
    report.confidence_level = "high";
    report.estimate_mode = "budget_conservative";
    report.free_tier_policy = "ignore_account_wide_free_tiers";
    report.free_tiers_applied = false;
    report.breakdown = nlohmann::json::array();
    return report;
}

inline std::uint32_t ownerForVaultDbTest(const std::uint32_t vaultId) {
    return vh::db::Transactions::exec("S3CostSafetyTest::ownerForVault", [&](pqxx::work& txn) {
        return txn.exec(
            "SELECT owner_id FROM vault WHERE id = $1",
            pqxx::params{vaultId}).one_field().as<std::uint32_t>();
    });
}

inline void attachS3ProviderForDbTest(const std::uint32_t vaultId, const std::string& provider) {
    vh::db::Transactions::exec("S3CostSafetyTest::attachS3Provider", [&](pqxx::work& txn) {
        const auto ownerId = txn.exec(
            "SELECT owner_id FROM vault WHERE id = $1",
            pqxx::params{vaultId}).one_field().as<std::uint32_t>();
        const auto apiKeyId = txn.exec(
            "INSERT INTO api_keys "
            "(user_id, name, provider, access_key, encrypted_secret_access_key, iv, region, endpoint) "
            "VALUES ($1, $2, $3, $4, decode('00','hex'), decode('00','hex'), $5, $6) RETURNING id",
            pqxx::params{
                ownerId,
                "budget-provider-" + std::to_string(vaultId),
                provider,
                "access-" + std::to_string(vaultId),
                "us-east-1",
                "https://s3.example.com"
            }).one_field().as<std::uint32_t>();
        txn.exec(
            "INSERT INTO s3 (vault_id, api_key_id, bucket) VALUES ($1, $2, $3) "
            "ON CONFLICT (vault_id) DO UPDATE SET api_key_id = EXCLUDED.api_key_id, bucket = EXCLUDED.bucket",
            pqxx::params{vaultId, apiKeyId, "bucket-" + std::to_string(vaultId)});
    });
}

inline vh::storage::s3::pricing::PriceBudgetPolicy saveVaultBudgetPolicyForDbTest(
    const std::uint32_t vaultId,
    std::optional<std::string> providerKey,
    const vh::storage::s3::pricing::PriceBudgetMode mode,
    const std::optional<std::string>& monthlyLimit,
    const bool requireVerified = true) {
    vh::storage::s3::pricing::PriceBudgetPolicy policy;
    policy.scope = vh::storage::s3::pricing::PriceBudgetScope::Vault;
    policy.vault_id = vaultId;
    policy.provider_key = std::move(providerKey);
    policy.mode = mode;
    policy.currency = "USD";
    policy.max_monthly_cost = monthlyLimit;
    policy.require_verified_catalog = requireVerified;
    policy.allow_stale_catalog = false;
    return vh::storage::s3::pricing::PriceBudgetService{}.upsertPolicy(std::move(policy));
}

inline uint32_t seedGatewayCredentialForDbTest(const uint32_t userId, const std::string& suffix) {
    vh::db::query::s3::GatewayCredential credential;
    credential.user_id = userId;
    credential.principal_user_id = userId;
    credential.created_by = userId;
    credential.name = "gateway-budget-" + suffix;
    credential.access_key = "VHTESTBUDGET" + suffix.substr(0, std::min<std::size_t>(suffix.size(), 32));
    std::ranges::replace(credential.access_key, '-', '_');
    credential.encrypted_secret_access_key = {1, 2, 3};
    credential.iv = {4, 5, 6};
    credential.enabled = true;
    credential.scope_mode = "user_access";
    return vh::db::query::s3::Gateway::createCredential(credential);
}

inline uint32_t s3CostVaultRoleIdByName(const std::string& roleName) {
    return vh::db::Transactions::exec("S3CostSafetyTest::vaultRoleIdByName", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            "SELECT id FROM vault_role WHERE name = $1 LIMIT 1",
            pqxx::params{roleName});
        if (!res.empty()) return res.one_field().as<uint32_t>();

        const auto role = [&]() {
            if (roleName == "implicit_deny") return vh::rbac::role::Vault::ImplicitDeny();
            if (roleName == "reader") return vh::rbac::role::Vault::Reader();
            if (roleName == "contributor") return vh::rbac::role::Vault::Contributor();
            if (roleName == "manager") return vh::rbac::role::Vault::Manager();
            throw std::runtime_error("vault role not found: " + roleName);
        }();
        return txn.exec(
            R"SQL(
                INSERT INTO vault_role (
                    name,
                    description,
                    files_permissions,
                    directories_permissions,
                    sync_permissions,
                    roles_permissions
                )
                VALUES ($1, $2, $3::bit(32), $4::bit(32), $5::bit(32), $6::bit(16))
                ON CONFLICT (name) DO UPDATE SET
                    description = EXCLUDED.description,
                    files_permissions = EXCLUDED.files_permissions,
                    directories_permissions = EXCLUDED.directories_permissions,
                    sync_permissions = EXCLUDED.sync_permissions,
                    roles_permissions = EXCLUDED.roles_permissions
                RETURNING id
            )SQL",
            pqxx::params{
                role.name,
                role.description,
                role.fs.files.toBitString(),
                role.fs.directories.toBitString(),
                role.sync.toBitString(),
                role.roles.toBitString()
            }).one_field().as<uint32_t>();
    });
}

inline std::optional<vh::db::query::s3::GatewayCredential> gatewayCredentialByIdForDbTest(const uint32_t credentialId) {
    const auto credentials = vh::db::query::s3::Gateway::listCredentialsAdmin(true);
    const auto it = std::ranges::find_if(credentials, [&](const auto& credential) {
        return credential.id == credentialId;
    });
    if (it == credentials.end()) return std::nullopt;
    return *it;
}

inline vh::storage::s3::pricing::PriceBudgetPolicy saveGatewayBudgetPolicyForDbTest(
    const vh::storage::s3::pricing::PriceBudgetScope scope,
    const uint32_t credentialId,
    const std::optional<uint32_t> vaultId,
    const vh::storage::s3::pricing::PriceBudgetMode mode,
    const std::string& monthlyLimit,
    const bool requireVerified = true) {
    vh::storage::s3::pricing::PriceBudgetPolicy policy;
    policy.scope = scope;
    policy.gateway_credential_id = credentialId;
    policy.vault_id = vaultId;
    policy.mode = mode;
    policy.currency = "USD";
    policy.max_monthly_cost = monthlyLimit;
    policy.require_verified_catalog = requireVerified;
    policy.allow_stale_catalog = false;
    return vh::storage::s3::pricing::PriceBudgetService{}.upsertPolicy(std::move(policy));
}

inline vh::storage::s3::pricing::PriceBudgetPolicy saveGenericBudgetPolicyForDbTest(
    const vh::storage::s3::pricing::PriceBudgetScope scope,
    std::optional<std::string> providerKey,
    std::optional<uint32_t> vaultId,
    const vh::storage::s3::pricing::PriceBudgetMode mode,
    const std::optional<std::string>& monthlyLimit,
    const std::optional<std::string>& dailyLimit = std::nullopt,
    const std::optional<std::string>& runLimit = std::nullopt) {
    vh::storage::s3::pricing::PriceBudgetPolicy policy;
    policy.scope = scope;
    policy.provider_key = std::move(providerKey);
    policy.vault_id = vaultId;
    policy.mode = mode;
    policy.currency = "USD";
    policy.max_run_cost = runLimit;
    policy.max_daily_cost = dailyLimit;
    policy.max_monthly_cost = monthlyLimit;
    policy.require_verified_catalog = false;
    policy.allow_stale_catalog = false;
    return vh::storage::s3::pricing::PriceBudgetService{}.upsertPolicy(std::move(policy));
}

inline std::uint32_t countPriceBudgetLedgerForRunDbTest(const std::string& runUuid) {
    return vh::db::Transactions::exec("S3CostSafetyTest::countPriceBudgetLedgerForRun", [&](pqxx::work& txn) {
        return txn.exec(
            "SELECT COUNT(*) AS c FROM s3_price_budget_ledger WHERE run_uuid = $1",
            pqxx::params{runUuid}).one_row()["c"].as<std::uint32_t>();
    });
}

inline std::uint32_t countGatewaySyncOriginForDbTest(
    const std::uint32_t vaultId,
    const std::string& objectKey,
    const std::string& operation) {
    return vh::db::Transactions::exec("S3CostSafetyTest::countGatewaySyncOrigin", [&](pqxx::work& txn) {
        return txn.exec(
            "SELECT COUNT(*) AS c FROM s3_gateway_sync_origin "
            "WHERE vault_id = $1 AND object_key = $2 AND operation = $3",
            pqxx::params{vaultId, objectKey, operation}).one_row()["c"].as<std::uint32_t>();
    });
}

inline std::shared_ptr<vh::storage::CloudEngine> makeDbBackedCloudEngine(
    const uint32_t vaultId,
    const std::shared_ptr<vh::storage::s3::Controller>& controller) {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = vaultId;
    vault->owner_id = 1;
    vault->type = vh::vault::model::VaultType::S3;
    vault->name = "s3-cost-safety-db";
    vault->mount_point = "s3-cost-safety-db";

    auto policy = std::make_shared<vh::sync::model::RemotePolicy>();
    policy->vault_id = vaultId;

    auto engine = std::make_shared<vh::storage::CloudEngine>();
    engine->vault = vault;
    engine->sync = policy;
    engine->setS3ControllerForTesting(controller);
    return engine;
}

inline uint32_t seedDryRunS3VaultForDbTest(
    const std::string& suffix,
    const std::shared_ptr<CountingS3Controller>& controller) {
    ensureSeededRuntimeReady();
    deleteIncompleteS3CostVaultFixturesForDbTest();

    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    if (!owner) throw std::runtime_error("admin user not available for dry-run test");

    auto key = std::make_shared<vh::vault::model::APIKey>(
        owner->id,
        "dry-run-key-" + suffix,
        vh::vault::model::S3Provider::AWS,
        "ABCDEFGHIJKLMNOPQRST",
        "ABCDEFGHIJKLMNOPQRSTABCDEFGHIJKLMNOPQRST",
        "us-east-1",
        "https://s3.example.com");
    vh::runtime::Deps::get().apiKeyManager->addAPIKey(key);

    auto vault = std::make_shared<vh::vault::model::S3Vault>(
        "dry-run-vault-" + suffix,
        key->id,
        "dry-run-bucket-" + suffix);
    vault->owner_id = owner->id;
    vault->description = "dry-run auth test";
    vault->mount_point = vh::crypto::id::Generator({.namespace_token = vault->name}).generate();

    auto policy = std::make_shared<vh::sync::model::RemotePolicy>();
    const auto vaultId = vh::db::query::vault::Vault::upsertVault(vault, policy);

    vh::runtime::Deps::get().storageManager->initStorageEngines();
    const auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(controller);
    return vaultId;
}

inline uint32_t seedLocalGatewayRouteVaultForDbTest(const std::string& suffix) {
    ensureSeededRuntimeReady();

    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    if (!owner) throw std::runtime_error("admin user not available for local gateway route budget test");

    const auto vaultId = vh::db::Transactions::exec("S3CostSafetyTest::seedLocalGatewayRouteVault", [&](pqxx::work& txn) {
        const auto mountPoint = vh::crypto::id::Generator({.namespace_token = "local-gateway-" + suffix}).generate();
        const auto seededVaultId = txn.exec(
            "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
            pqxx::params{
                "local",
                "Local Gateway Route Budget " + suffix,
                owner->id,
                mountPoint,
                "local gateway route budget test"
            }).one_field().as<uint32_t>();
        txn.exec(
            "WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
            "INSERT INTO fsync (sync_id, conflict_policy) SELECT id, 'keep_both' FROM ins",
            pqxx::params{seededVaultId});
        return seededVaultId;
    });

    vh::runtime::Deps::get().storageManager->initStorageEngines();
    return vaultId;
}

inline std::shared_ptr<vh::identities::User> dryRunActor(
    const uint32_t id,
    vh::rbac::role::Admin role) {
    auto user = std::make_shared<vh::identities::User>();
    user->id = id;
    user->name = "dry-run-actor-" + std::to_string(id);
    user->password_hash = "hash";
    user->roles.admin = std::make_shared<vh::rbac::role::Admin>(std::move(role));
    return user;
}

inline std::shared_ptr<vh::protocols::ws::Session> superAdminWsSession() {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    auto user = std::make_shared<vh::identities::User>();
    user->id = 1;
    user->name = "admin";
    user->password_hash = "hash";
    user->roles.admin = std::make_shared<vh::rbac::role::Admin>(
        vh::rbac::role::Admin::SuperAdmin(user->id));
    session->user = user;
    return session;
}

inline std::shared_ptr<vh::protocols::ws::Session> wsSessionForUser(
    const uint32_t userId,
    vh::rbac::role::Admin role = vh::rbac::role::Admin::None()) {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    session->user = dryRunActor(userId, std::move(role));
    return session;
}

inline std::shared_ptr<vh::protocols::shell::Router> s3GatewayShellRouterForDbTest() {
    ensureWritableTestPathRoots();
    if (!vh::runtime::Deps::get().shellUsageManager)
        vh::runtime::Deps::get().shellUsageManager = std::make_shared<vh::protocols::shell::UsageManager>();
    auto router = std::make_shared<vh::protocols::shell::Router>();
    vh::protocols::shell::commands::registerS3GatewayCommands(router);
    return router;
}

inline vh::protocols::shell::CommandResult runDryRunCommand(
    const uint32_t vaultId,
    const std::shared_ptr<vh::identities::User>& user,
    const bool refreshIndex = false) {
    vh::protocols::shell::CommandCall call;
    call.name = "vault";
    call.user = user;
    call.positionals = {"dry-run", std::to_string(vaultId)};
    if (refreshIndex) call.options.push_back({"refresh-index", std::nullopt});
    return vh::protocols::shell::commands::vault::handle_sync(call);
}

inline uint32_t seedLegacyRsyncPolicyForDbTest(
    const std::string& suffix,
    const std::optional<uint64_t> customGetBudget = std::nullopt) {
    const auto vaultId = seedS3VaultForDbTest(suffix);
    vh::db::Transactions::exec("S3CostSafetyTest::seedLegacyRsyncPolicy", [&](pqxx::work& txn) {
        const auto syncId = txn.exec(
            "INSERT INTO sync (vault_id) VALUES ($1) RETURNING id",
            pqxx::params{vaultId}).one_field().as<uint32_t>();

        if (customGetBudget) {
            txn.exec(
                "INSERT INTO rsync (sync_id, s3_budget_get_requests) VALUES ($1, $2)",
                pqxx::params{syncId, *customGetBudget});
        } else {
            txn.exec("INSERT INTO rsync (sync_id) VALUES ($1)", pqxx::params{syncId});
        }
    });
    return vaultId;
}

inline void applyS3BudgetBackfillMigrationForDbTest() {
    vh::db::Transactions::exec("S3CostSafetyTest::applyS3BudgetBackfillMigration", [&](pqxx::work& txn) {
        const auto migration = vh::paths::getPsqlSchemasPath() / "088_backfill_s3_budget_defaults.sql";
        txn.exec(vh::db::seed::readFileToString(migration));
    });
}

inline std::shared_ptr<vh::sync::model::RemotePolicy> loadRemotePolicyForDbTest(const uint32_t vaultId) {
    return vh::db::Transactions::exec("S3CostSafetyTest::loadRemotePolicy", [&](pqxx::work& txn) {
        const auto res = txn.exec(
            "SELECT rs.*, s.* "
            "FROM rsync rs JOIN sync s ON s.id = rs.sync_id "
            "WHERE s.vault_id = $1",
            pqxx::params{vaultId});
        return std::make_shared<vh::sync::model::RemotePolicy>(res.one_row());
    });
}

class ScopedPathRoots {
public:
    explicit ScopedPathRoots(std::filesystem::path root)
        : root_(std::move(root)),
          oldBackingPath_(vh::paths::backingPath),
          oldMountPath_(vh::paths::mountPath) {
        std::filesystem::remove_all(root_);
        vh::paths::backingPath = root_ / "backing";
        vh::paths::mountPath = root_ / "mount";
        std::filesystem::create_directories(vh::paths::backingPath);
        std::filesystem::create_directories(vh::paths::mountPath);
    }

    ~ScopedPathRoots() {
        vh::paths::backingPath = oldBackingPath_;
        vh::paths::mountPath = oldMountPath_;
        std::filesystem::remove_all(root_);
    }

private:
    std::filesystem::path root_;
    std::filesystem::path oldBackingPath_;
    std::filesystem::path oldMountPath_;
};

inline std::shared_ptr<vh::storage::CloudEngine> makePlanningEngine() {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 77;
    vault->owner_id = 88;
    vault->quota = 1024 * 1024 * 1024;
    vault->name = "s3-cost-safety";
    vault->mount_point = "s3-cost-safety";

    auto policy = std::make_shared<vh::sync::model::RemotePolicy>();
    policy->vault_id = vault->id;

    auto engine = std::make_shared<vh::storage::CloudEngine>();
    engine->vault = vault;
    engine->sync = policy;
    return engine;
}

inline std::shared_ptr<vh::fs::model::File> remoteFile(
    const std::string& key,
    const std::optional<std::string>& storageClass = std::nullopt) {
    auto file = std::make_shared<vh::fs::model::File>(key, 42, std::time(nullptr));
    if (storageClass) file->remote_storage_class = *storageClass;
    return file;
}

inline void configureS3GatewayRouteBudgetConfig() {
    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.require_sigv4 = true;
    cfg.s3_gateway.allow_path_style = true;
    cfg.s3_gateway.allow_virtual_hosted_style = false;
    cfg.pricing.enabled = true;
    cfg.pricing.storage_rates_api.remote_refresh_enabled = false;
    cfg.pricing.storage_rates_api.signature_warning_only = true;
    cfg.pricing.storage_rates_api.signature_public_key_path.reset();
    vh::config::Registry::set(cfg);
}

struct GatewayRouteBudgetFixture {
    uint32_t vault_id{0};
    std::string bucket_name;
    std::shared_ptr<CountingS3Controller> controller;
    vh::protocols::s3::GatewaySecret secret;
};

inline GatewayRouteBudgetFixture setupGatewayRouteBudgetFixture(
    const std::string& label,
    const vh::storage::s3::pricing::PriceBudgetScope scope,
    const std::string& monthlyLimit,
    const vh::storage::s3::pricing::PriceBudgetMode mode = vh::storage::s3::pricing::PriceBudgetMode::Enforce,
    const bool enforceBudgetForLocalRequests = false) {
    ensureSeededRuntimeReady();
    seedAwsGatewayRoutePriceCatalog();

    auto fake = std::make_shared<CountingS3Controller>();
    const auto suffix = uniqueSuffix(label);
    const auto vaultId = seedDryRunS3VaultForDbTest(suffix, fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);
    std::static_pointer_cast<vh::vault::model::S3Vault>(engine->vault)->encrypt_upstream = false;

    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    if (!owner) throw std::runtime_error("admin user not available for S3 gateway route budget test");

    const auto bucketName = uniqueBucketName(label);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "remote_cache",
        .created_by = owner->id
    });

    const vh::protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential({
        .created_by = owner->id,
        .principal_user_id = owner->id,
        .name = "route-budget-" + suffix,
        .scope_mode = "user_access",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {},
        .enforce_budget_for_local_requests = enforceBudgetForLocalRequests
    });

    saveGatewayBudgetPolicyForDbTest(
        scope,
        secret.credential.id,
        scope == vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault
            ? std::make_optional(vaultId)
            : std::optional<uint32_t>{},
        mode,
        monthlyLimit,
        false);

    return {
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .controller = fake,
        .secret = std::move(secret)
    };
}

inline GatewayRouteBudgetFixture setupLocalGatewayRouteBudgetFixture(
    const std::string& label,
    const vh::storage::s3::pricing::PriceBudgetScope scope,
    const std::string& monthlyLimit,
    const vh::storage::s3::pricing::PriceBudgetMode mode = vh::storage::s3::pricing::PriceBudgetMode::Enforce,
    const bool enforceBudgetForLocalRequests = false) {
    ensureSeededRuntimeReady();

    const auto suffix = uniqueSuffix(label);
    const auto vaultId = seedLocalGatewayRouteVaultForDbTest(suffix);
    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    if (!owner) throw std::runtime_error("admin user not available for local S3 gateway route budget test");

    const auto bucketName = uniqueBucketName(label);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "local",
        .created_by = owner->id
    });

    const vh::protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential({
        .created_by = owner->id,
        .principal_user_id = owner->id,
        .name = "route-local-budget-" + suffix,
        .scope_mode = "user_access",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {},
        .enforce_budget_for_local_requests = enforceBudgetForLocalRequests
    });

    saveGatewayBudgetPolicyForDbTest(
        scope,
        secret.credential.id,
        scope == vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault
            ? std::make_optional(vaultId)
            : std::optional<uint32_t>{},
        mode,
        monthlyLimit,
        false);

    return {
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .controller = nullptr,
        .secret = std::move(secret)
    };
}

inline void setGatewayRouteRequestBudget(
    const GatewayRouteBudgetFixture& fixture,
    const std::function<void(vh::storage::s3::S3RequestBudget&)>& mutate) {
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(fixture.vault_id));
    auto policy = engine->remote_policy();
    ASSERT_TRUE(policy);
    policy->s3_request_budget = {};
    mutate(policy->s3_request_budget);
}

inline vh::protocols::s3::Router::Request signedGatewayRouteRequest(
    const http::verb method,
    const std::string& target,
    const vh::protocols::s3::GatewaySecret& secret,
    const std::string& body = {}) {
    vh::protocols::s3::Router::Request request{method, target, 11};
    request.set(http::field::host, "localhost:39000");
    request.body() = body;
    request.prepare_payload();
    signGatewayRouteRequest(request, secret.credential.access_key, secret.secret_access_key);
    return request;
}

inline std::string textBetween(const std::string& text, const std::string& open, const std::string& close) {
    const auto start = text.find(open);
    if (start == std::string::npos) return {};
    const auto valueStart = start + open.size();
    const auto end = text.find(close, valueStart);
    if (end == std::string::npos) return {};
    return text.substr(valueStart, end - valueStart);
}

inline void expectGatewayLedgerCommitted(
    const GatewayRouteBudgetFixture& fixture,
    const std::string& operation,
    const std::string& objectKey,
    const std::optional<bool> synthetic = std::nullopt,
    const std::optional<std::string>& usageSource = std::nullopt) {
    const auto ledger = vh::storage::s3::pricing::PriceBudgetService{}.listLedger(
        20,
        fixture.vault_id,
        fixture.secret.credential.id);
    const auto it = std::ranges::find_if(ledger, [&](const auto& entry) {
        return entry.operation && *entry.operation == operation &&
            entry.object_key && *entry.object_key == objectKey;
    });
    ASSERT_NE(it, ledger.end());
    EXPECT_EQ(fixture.vault_id, it->vault_id);
    ASSERT_TRUE(it->gateway_credential_id);
    EXPECT_EQ(fixture.secret.credential.id, *it->gateway_credential_id);
    EXPECT_TRUE(it->request_uuid);
    EXPECT_TRUE(it->estimated_cost);
    EXPECT_EQ("committed", it->status);
    ASSERT_TRUE(it->committed_cost);
    if (synthetic) {
        EXPECT_EQ(*synthetic, it->synthetic);
    }
    if (usageSource) {
        ASSERT_TRUE(it->usage_source);
        EXPECT_EQ(*usageSource, *it->usage_source);
    }
}

inline void expectGatewayLedgerAbsent(
    const GatewayRouteBudgetFixture& fixture,
    const std::string& operation,
    const std::string& objectKey) {
    const auto ledger = vh::storage::s3::pricing::PriceBudgetService{}.listLedger(
        20,
        fixture.vault_id,
        fixture.secret.credential.id);
    const auto it = std::ranges::find_if(ledger, [&](const auto& entry) {
        return entry.operation && *entry.operation == operation &&
            entry.object_key && *entry.object_key == objectKey &&
            entry.status != "released" &&
            entry.status != "expired";
    });
    EXPECT_EQ(it, ledger.end());
}


} // namespace vh::test::s3_cost_safety
