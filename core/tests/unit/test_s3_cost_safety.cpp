#include "db/encoding/interval.hpp"
#include "db/Transactions.hpp"
#include "db/query/fs/File.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "db/query/identities/User.hpp"
#include "db/query/vault/Vault.hpp"
#include "crypto/id/Generator.hpp"
#include "fs/Filesystem.hpp"
#include "fs/model/Path.hpp"
#include "fs/model/File.hpp"
#include "identities/User.hpp"
#include "protocols/shell/commands/vault.hpp"
#include "rbac/role/Admin.hpp"
#include "runtime/Deps.hpp"
#include "seed/include/init_db_tables.hpp"
#include "seed/include/seed_db.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/Manager.hpp"
#include "storage/ScopedS3RequestBudget.hpp"
#include "storage/s3/Controller.hpp"
#include "storage/s3/curl/helpers.hpp"
#include "sync/Cloud.hpp"
#include "sync/Planner.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemoteManifest.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "sync/model/ScopedOp.hpp"
#include "sync/tasks/Delete.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"
#include "vault/APIKeyManager.hpp"
#include "vault/EncryptionManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::shared_ptr<vh::vault::model::APIKey> dummyApiKey() {
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
    int delete_object_calls = 0;
    std::unordered_map<std::string, std::string> last_metadata;
    std::optional<std::unordered_map<std::string, std::string>> head_response;
    std::vector<uint8_t> download_payload;

    CountingS3Controller()
        : Controller(dummyApiKey(), "unit-bucket") {}

    void uploadObjectWithMetadata(
        const std::filesystem::path&,
        const std::filesystem::path&,
        const std::unordered_map<std::string, std::string>& metadata) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->upload_object_with_metadata_calls;
        self->last_metadata = metadata;
    }

    void uploadLargeObject(
        const std::filesystem::path&,
        const std::filesystem::path&,
        uintmax_t,
        const std::unordered_map<std::string, std::string>& metadata) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        self->last_metadata = metadata;
    }

    void downloadToBuffer(const std::filesystem::path&, std::vector<uint8_t>& out) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->download_to_buffer_calls;
        self->recordRequest(RequestKind::Get);
        out = self->download_payload;
    }

    std::optional<std::unordered_map<std::string, std::string>> getHeadObject(
        const std::filesystem::path&) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->head_object_calls;
        self->recordRequest(RequestKind::Head);
        return head_response;
    }

    void deleteObject(const std::filesystem::path&) const override {
        auto* self = const_cast<CountingS3Controller*>(this);
        ++self->delete_object_calls;
        self->recordRequest(RequestKind::Delete);
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

bool hasDbEnv() {
    return std::getenv("VH_TEST_DB_USER") &&
           std::getenv("VH_TEST_DB_PASS") &&
           std::getenv("VH_TEST_DB_HOST") &&
           std::getenv("VH_TEST_DB_PORT") &&
           std::getenv("VH_TEST_DB_NAME");
}

std::string uniqueSuffix(const std::string& label) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return label + "_" + std::to_string(ticks);
}

void ensureDbReady() {
    static bool initialized = false;
    if (initialized) return;

    vh::db::Transactions::init();
    vh::db::seed::nuke_and_recreate_schema_public();
    vh::db::Transactions::dbPool_->initPreparedStatements();
    initialized = true;
}

void ensureWritableTestPathRoots() {
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

void ensureSeededRuntimeReady() {
    static bool seeded = false;
    ensureWritableTestPathRoots();
    ensureDbReady();
    if (!seeded) {
        vh::seed::seed_database();
        vh::runtime::Deps::init();
        vh::fs::Filesystem::init(vh::runtime::Deps::get().storageManager);
        seeded = true;
    }
}

uint32_t seedS3VaultForDbTest(const std::string& suffix) {
    return vh::db::Transactions::exec("S3CostSafetyTest::seedS3Vault", [&](pqxx::work& txn) {
        const auto userId = txn.exec(
            "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
            pqxx::params{
                "s3_cost_safety_user_" + suffix,
                "s3-cost-safety-" + suffix + "@vaulthalla.test",
                "hash"
            }).one_field().as<uint32_t>();

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

std::shared_ptr<vh::storage::CloudEngine> makeDbBackedCloudEngine(
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

uint32_t seedDryRunS3VaultForDbTest(
    const std::string& suffix,
    const std::shared_ptr<CountingS3Controller>& controller) {
    ensureSeededRuntimeReady();
    vh::db::Transactions::exec("S3CostSafetyTest::deleteIncompleteS3VaultFixtures", [&](pqxx::work& txn) {
        txn.exec(
            "DELETE FROM vault v "
            "WHERE v.type = 's3' "
            "AND NOT EXISTS (SELECT 1 FROM s3 WHERE s3.vault_id = v.id)");
    });

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

std::shared_ptr<vh::identities::User> dryRunActor(
    const uint32_t id,
    vh::rbac::role::Admin role) {
    auto user = std::make_shared<vh::identities::User>();
    user->id = id;
    user->name = "dry-run-actor-" + std::to_string(id);
    user->password_hash = "hash";
    user->roles.admin = std::make_shared<vh::rbac::role::Admin>(std::move(role));
    return user;
}

vh::protocols::shell::CommandResult runDryRunCommand(
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

uint32_t seedLegacyRsyncPolicyForDbTest(
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

void applyS3BudgetBackfillMigrationForDbTest() {
    vh::db::Transactions::exec("S3CostSafetyTest::applyS3BudgetBackfillMigration", [&](pqxx::work& txn) {
        const auto migration = vh::paths::getPsqlSchemasPath() / "088_backfill_s3_budget_defaults.sql";
        txn.exec(vh::db::seed::readFileToString(migration));
    });
}

std::shared_ptr<vh::sync::model::RemotePolicy> loadRemotePolicyForDbTest(const uint32_t vaultId) {
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

std::shared_ptr<vh::storage::CloudEngine> makePlanningEngine() {
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

std::shared_ptr<vh::fs::model::File> remoteFile(
    const std::string& key,
    const std::optional<std::string>& storageClass = std::nullopt) {
    auto file = std::make_shared<vh::fs::model::File>(key, 42, std::time(nullptr));
    if (storageClass) file->remote_storage_class = *storageClass;
    return file;
}

} // namespace

TEST(S3CostSafetyTest, PolicyIntervalsDefaultAndClampToNonZero) {
    vh::sync::model::RemotePolicy remote;
    EXPECT_EQ(300, remote.interval.count());
    EXPECT_TRUE(remote.enabled);
    EXPECT_EQ(
        "balanced",
        vh::sync::model::s3BudgetPresetName(remote.s3_request_budget));
    EXPECT_TRUE(remote.s3_request_budget.max_list_requests.has_value());
    ASSERT_TRUE(remote.max_remote_index_age);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::hours(24)).count(), remote.max_remote_index_age->count());
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::hours(2)).count(), vh::sync::model::remoteIndexAgeFromString("2h")->count());
    EXPECT_FALSE(vh::sync::model::remoteIndexAgeFromString("unlimited").has_value());

    EXPECT_EQ(300, vh::sync::model::Policy::clampInterval(std::chrono::seconds(0)).count());
    EXPECT_EQ(300, vh::sync::model::Policy::clampInterval(std::chrono::seconds(-5)).count());
    EXPECT_EQ(15, vh::sync::model::Policy::clampInterval(std::chrono::seconds(15)).count());
    EXPECT_EQ(300, vh::db::encoding::parseSyncInterval("").count());
}

TEST(S3CostSafetyTest, EngineFreeSpaceSaturatesFiniteQuota) {
    ScopedPathRoots paths(std::filesystem::temp_directory_path() / "vh_engine_quota_finite");

    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->id = 1001;
    vault->name = "quota-test";
    vault->mount_point = "quota-test";
    vault->quota = vh::storage::Engine::MIN_FREE_SPACE + 1024;

    vh::storage::Engine engine;
    engine.vault = vault;
    engine.paths = std::make_shared<vh::fs::model::Path>("quota-test", "quota-test");
    std::filesystem::create_directories(engine.paths->backingVaultRoot);
    std::filesystem::create_directories(engine.paths->cacheRoot);

    EXPECT_EQ(1024u, engine.freeSpace());

    std::ofstream(engine.paths->backingVaultRoot / "used.bin", std::ios::binary) << std::string(200, 'x');
    EXPECT_EQ(824u, engine.freeSpace());

    vault->quota = vh::storage::Engine::MIN_FREE_SPACE - 1;
    EXPECT_EQ(0u, engine.freeSpace());
}

TEST(S3CostSafetyTest, EngineFreeSpaceTreatsZeroQuotaAsUnlimitedWithoutUnderflow) {
    ScopedPathRoots paths(std::filesystem::temp_directory_path() / "vh_engine_quota_unlimited");

    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->id = 1002;
    vault->name = "quota-unlimited-test";
    vault->mount_point = "quota-unlimited-test";
    vault->quota = 0;

    vh::storage::Engine engine;
    engine.vault = vault;
    engine.paths = std::make_shared<vh::fs::model::Path>("quota-unlimited-test", "quota-unlimited-test");
    std::filesystem::create_directories(engine.paths->backingVaultRoot);
    std::filesystem::create_directories(engine.paths->cacheRoot);

    std::error_code ec;
    const auto available = std::filesystem::space(engine.paths->backingRoot, ec).available;
    ASSERT_FALSE(ec);
    const auto expected = available > vh::storage::Engine::MIN_FREE_SPACE
        ? available - vh::storage::Engine::MIN_FREE_SPACE
        : 0;

    EXPECT_EQ(expected, engine.freeSpace());
    EXPECT_LE(engine.freeSpace(), available);
}

TEST(S3CostSafetyTest, UnlimitedBudgetPolicyPrintsLegacyWarning) {
    auto remote = std::make_shared<vh::sync::model::RemotePolicy>();
    remote->s3_request_budget = vh::sync::model::s3RequestBudgetForPreset(
        vh::sync::model::S3BudgetPreset::Unlimited);

    const auto text = vh::sync::model::to_string(remote);
    EXPECT_NE(std::string::npos, text.find("unlimited/legacy budget"));
    EXPECT_NE(std::string::npos, text.find("--s3-budget-preset balanced"));
}

TEST(S3CostSafetyTest, MigrationBackfillsAllNullLegacyS3BudgetsToBalancedPreset) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 budget migration test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedLegacyRsyncPolicyForDbTest(uniqueSuffix("budget_backfill"));
    applyS3BudgetBackfillMigrationForDbTest();

    const auto policy = loadRemotePolicyForDbTest(vaultId);
    const auto balanced = vh::sync::model::s3RequestBudgetForPreset(vh::sync::model::S3BudgetPreset::Balanced);
    EXPECT_EQ(balanced.max_list_requests, policy->s3_request_budget.max_list_requests);
    EXPECT_EQ(balanced.max_head_requests, policy->s3_request_budget.max_head_requests);
    EXPECT_EQ(balanced.max_get_requests, policy->s3_request_budget.max_get_requests);
    EXPECT_EQ(balanced.max_put_requests, policy->s3_request_budget.max_put_requests);
    EXPECT_EQ(balanced.max_copy_requests, policy->s3_request_budget.max_copy_requests);
    EXPECT_EQ(balanced.max_delete_requests, policy->s3_request_budget.max_delete_requests);
    EXPECT_EQ(balanced.max_downloaded_bytes, policy->s3_request_budget.max_downloaded_bytes);
    EXPECT_EQ("balanced", vh::sync::model::s3BudgetPresetName(policy->s3_request_budget));
    EXPECT_EQ(100u, policy->s3_request_budget.max_list_requests.value_or(0));

    const auto text = vh::sync::model::to_string(policy);
    EXPECT_NE(std::string::npos, text.find("Preset: balanced"));
    EXPECT_EQ(std::string::npos, text.find("unlimited/legacy budget"));
}

TEST(S3CostSafetyTest, MigrationDoesNotOverwriteCustomS3BudgetRows) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 budget migration test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedLegacyRsyncPolicyForDbTest(uniqueSuffix("budget_custom"), 42);
    applyS3BudgetBackfillMigrationForDbTest();

    const auto policy = loadRemotePolicyForDbTest(vaultId);
    EXPECT_FALSE(policy->s3_request_budget.max_list_requests.has_value());
    ASSERT_TRUE(policy->s3_request_budget.max_get_requests.has_value());
    EXPECT_EQ(42u, *policy->s3_request_budget.max_get_requests);
    EXPECT_FALSE(policy->s3_request_budget.max_head_requests.has_value());
    EXPECT_FALSE(policy->s3_request_budget.max_put_requests.has_value());
    EXPECT_FALSE(policy->s3_request_budget.max_copy_requests.has_value());
    EXPECT_FALSE(policy->s3_request_budget.max_delete_requests.has_value());
    EXPECT_FALSE(policy->s3_request_budget.max_downloaded_bytes.has_value());
    EXPECT_EQ("custom", vh::sync::model::s3BudgetPresetName(policy->s3_request_budget));
}

TEST(S3CostSafetyTest, DryRunViewOnlyUsesFreshLocalIndexWithoutS3Refresh) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed dry-run auth test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("dryrun_view"), fake);
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remoteFile("remote.txt"), "manifest");

    const auto viewOnly = dryRunActor(
        90'001,
        vh::rbac::role::Admin::Auditor(90'001));
    const auto before = vh::db::query::sync::RemoteObjectIndex::summaryForVault(vaultId);

    const auto result = runDryRunCommand(vaultId, viewOnly);
    const auto after = vh::db::query::sync::RemoteObjectIndex::summaryForVault(vaultId);

    EXPECT_EQ(0, result.exit_code) << result.stderr_text;
    EXPECT_EQ(0, fake->head_object_calls);
    EXPECT_EQ(0, fake->download_to_buffer_calls);
    EXPECT_EQ(0u, fake->requestMetrics().head_requests);
    EXPECT_EQ(0u, fake->requestMetrics().get_requests);
    EXPECT_EQ(before.manifest_updated_at, after.manifest_updated_at);
    EXPECT_EQ(std::string::npos, result.stdout_text.find("may refresh"));
}

TEST(S3CostSafetyTest, DryRunRefreshIndexRequiresTriggerPermission) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed dry-run auth test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("dryrun_refresh_denied"), fake);
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remoteFile("remote.txt"), "manifest");

    const auto viewOnly = dryRunActor(
        90'002,
        vh::rbac::role::Admin::Auditor(90'002));

    const auto result = runDryRunCommand(vaultId, viewOnly, true);

    EXPECT_EQ(2, result.exit_code);
    EXPECT_NE(std::string::npos, result.stderr_text.find("permission to refresh"));
    EXPECT_EQ(0, fake->head_object_calls);
    EXPECT_EQ(0, fake->download_to_buffer_calls);
}

TEST(S3CostSafetyTest, DryRunRefreshIndexWithTriggerMayUseS3HeadMetrics) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed dry-run auth test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("dryrun_refresh_allowed"), fake);
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remoteFile("remote.txt"), "manifest");

    const auto trigger = dryRunActor(
        90'003,
        vh::rbac::role::Admin::PlatformOperator(90'003));

    const auto result = runDryRunCommand(vaultId, trigger, true);

    EXPECT_EQ(0, result.exit_code) << result.stderr_text;
    EXPECT_EQ(1, fake->head_object_calls);
    EXPECT_EQ(0, fake->download_to_buffer_calls);
    EXPECT_EQ(1u, fake->requestMetrics().head_requests);
    EXPECT_NE(std::string::npos, result.stdout_text.find("Note: --refresh-index may refresh the remote index manifest before planning."));
    EXPECT_NE(std::string::npos, result.stdout_text.find("HEAD: 1"));
}

TEST(S3CostSafetyTest, DryRunReportsMissingAndStaleLocalIndexWithoutS3Refresh) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed dry-run auth test due to missing environment variables.";

    const auto viewOnly = dryRunActor(
        90'004,
        vh::rbac::role::Admin::Auditor(90'004));

    auto missingFake = std::make_shared<CountingS3Controller>();
    const auto missingVaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("dryrun_missing"), missingFake);
    const auto missing = runDryRunCommand(missingVaultId, viewOnly);
    EXPECT_EQ(2, missing.exit_code);
    EXPECT_EQ(
        "vault sync dry-run: no remote index is available; run reconcile, inventory import, event ingestion, or dry-run --refresh-index.",
        missing.stderr_text);
    EXPECT_EQ(0, missingFake->head_object_calls);

    auto staleFake = std::make_shared<CountingS3Controller>();
    const auto staleVaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("dryrun_stale"), staleFake);
    vh::db::Transactions::exec("S3CostSafetyTest::seedStaleDryRunIndex", [&](pqxx::work& txn) {
        txn.exec(
            "INSERT INTO remote_object_index "
            "(vault_id, object_key, size_bytes, last_modified, etag, source, indexed_at) "
            "VALUES ($1, $2, $3, CURRENT_TIMESTAMP - INTERVAL '2 hours', $4, $5, CURRENT_TIMESTAMP - INTERVAL '2 hours')",
            pqxx::params{staleVaultId, "stale.txt", 1, "\"stale\"", "manifest"});
    });
    auto staleEngine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(staleVaultId));
    staleEngine->remote_policy()->max_remote_index_age = std::chrono::seconds(60);

    const auto stale = runDryRunCommand(staleVaultId, viewOnly);
    EXPECT_EQ(2, stale.exit_code);
    EXPECT_EQ(
        "vault sync dry-run: remote index is stale; run dry-run --refresh-index, reconcile, inventory import, or event ingestion.",
        stale.stderr_text);
    EXPECT_EQ(0, staleFake->head_object_calls);
}

TEST(S3CostSafetyTest, DbFileBackingPathOmitsGlobalRootAlias) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed backing path reconstruction test due to missing environment variables.";

    ensureSeededRuntimeReady();
    const auto suffix = uniqueSuffix("entry_backing");
    const auto mountAlias = vh::crypto::id::Generator({.namespace_token = "vault-" + suffix}).generate();
    const auto dirAlias = vh::crypto::id::Generator({.namespace_token = "dir-" + suffix}).generate();
    const auto fileAlias = vh::crypto::id::Generator({.namespace_token = "file-" + suffix}).generate();
    const auto dirName = "folder-" + suffix;
    const auto fileName = "file-" + suffix + ".txt";
    const auto filePath = std::filesystem::path("/") / dirName / fileName;

    const auto vaultId = vh::db::Transactions::exec("S3CostSafetyTest::seedEntryBackingPathRows", [&](pqxx::work& txn) {
        const auto userId = txn.exec(
            "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
            pqxx::params{
                "entry_backing_user_" + suffix,
                "entry-backing-" + suffix + "@vaulthalla.test",
                "hash"
            }).one_field().as<uint32_t>();

        const auto seededVaultId = txn.exec(
            "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
            pqxx::params{
                "local",
                "Entry Backing " + suffix,
                userId,
                mountAlias,
                ""
            }).one_field().as<uint32_t>();
        txn.exec(
            "WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
            "INSERT INTO fsync (sync_id, conflict_policy) SELECT id, 'keep_both' FROM ins",
            pqxx::params{seededVaultId});

        const auto rootId = txn.exec(
            "SELECT id FROM fs_entry WHERE parent_id IS NULL AND vault_id IS NULL AND path = '/' AND name = '/'"
        ).one_field().as<uint32_t>();

        const auto vaultEntryId = txn.exec(
            "INSERT INTO fs_entry (vault_id, parent_id, name, base32_alias, created_by, last_modified_by, path, mode) "
            "VALUES ($1, $2, $3, $4, $5, $5, '/', 0755) RETURNING id",
            pqxx::params{seededVaultId, rootId, "entry_backing_" + suffix, mountAlias, userId}
        ).one_field().as<uint32_t>();
        txn.exec(
            "INSERT INTO directories (fs_entry_id, size_bytes, file_count, subdirectory_count) VALUES ($1, 4, 1, 1)",
            pqxx::params{vaultEntryId});

        const auto dirId = txn.exec(
            "INSERT INTO fs_entry (vault_id, parent_id, name, base32_alias, created_by, last_modified_by, path, mode) "
            "VALUES ($1, $2, $3, $4, $5, $5, $6, 0755) RETURNING id",
            pqxx::params{seededVaultId, vaultEntryId, dirName, dirAlias, userId, "/" + dirName}
        ).one_field().as<uint32_t>();
        txn.exec(
            "INSERT INTO directories (fs_entry_id, size_bytes, file_count, subdirectory_count) VALUES ($1, 4, 1, 0)",
            pqxx::params{dirId});

        const auto fileId = txn.exec(
            "INSERT INTO fs_entry (vault_id, parent_id, name, base32_alias, created_by, last_modified_by, path, mode) "
            "VALUES ($1, $2, $3, $4, $5, $5, $6, 0644) RETURNING id",
            pqxx::params{seededVaultId, dirId, fileName, fileAlias, userId, filePath.string()}
        ).one_field().as<uint32_t>();
        txn.exec(
            "INSERT INTO files (fs_entry_id, size_bytes, mime_type, content_hash, encryption_iv) "
            "VALUES ($1, 4, 'text/plain', 'hash', 'iv')",
            pqxx::params{fileId});

        return seededVaultId;
    });

    const auto file = vh::db::query::fs::File::getFileByPath(vaultId, filePath);
    ASSERT_TRUE(file);
    EXPECT_EQ(vh::paths::getBackingPath() / mountAlias / dirAlias / fileAlias, file->backing_path);
}

TEST(S3CostSafetyTest, SigV4SignedHeadersIncludeMetadataHeaders) {
    const std::map<std::string, std::string> headers{
        {"content-type", "application/octet-stream"},
        {"host", "s3.example.com"},
        {"x-amz-content-sha256", vh::storage::s3::curl::sha256Hex("ciphertext")},
        {"x-amz-date", "20260525T000000Z"},
        {"x-amz-meta-vh-encrypted", "true"},
        {"x-amz-meta-vh-iv", "iv"},
        {"x-amz-meta-vh-key-version", "3"},
    };

    const auto auth = vh::storage::s3::curl::buildAuthorizationHeader(
        dummyApiKey(),
        "PUT",
        "/unit-bucket/ciphertext.bin",
        headers,
        headers.at("x-amz-content-sha256"));

    EXPECT_NE(
        std::string::npos,
        auth.find(
            "SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date;"
            "x-amz-meta-vh-encrypted;x-amz-meta-vh-iv;x-amz-meta-vh-key-version"));
}

TEST(S3CostSafetyTest, RemoteEncryptedPayloadUsesCaseInsensitiveHeadMetadata) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed encrypted remote metadata test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("head_iv_case"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const std::vector<uint8_t> plaintext{'s', 'e', 'c', 'r', 'e', 't'};
    auto encryptedSource = remoteFile("mixed-case-head.bin");
    const auto ciphertext = engine->encryptionManager->encrypt(plaintext, encryptedSource);

    fake->head_response = std::unordered_map<std::string, std::string>{
        {"X-Amz-Meta-Vh-Encrypted", "true"},
        {"X-Amz-Meta-Vh-Iv", encryptedSource->encryption_iv},
        {"X-Amz-Meta-Vh-Key-Version", std::to_string(encryptedSource->encrypted_with_key_version)},
    };

    auto remote = remoteFile("mixed-case-head.bin");
    remote->remote_encrypted = true;
    const auto decrypted = engine->decryptRemotePayload(remote->path, ciphertext, remote);

    EXPECT_EQ(plaintext, decrypted);
    EXPECT_EQ(1, fake->head_object_calls);
}

TEST(S3CostSafetyTest, RemoteObjectIndexRoundTripsEncryptionMetadata) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index metadata test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("remote_index_iv"));
    auto remote = remoteFile("encrypted/from-index.txt");
    remote->content_hash = "content-hash";
    remote->remote_encrypted = true;
    remote->encryption_iv = "iv-from-index";
    remote->encrypted_with_key_version = 7;

    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remote, "manifest");
    const auto files = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);

    ASSERT_EQ(1u, files.size());
    EXPECT_EQ("/encrypted/from-index.txt", files[0]->path.string());
    ASSERT_TRUE(files[0]->content_hash);
    EXPECT_EQ("content-hash", *files[0]->content_hash);
    ASSERT_TRUE(files[0]->remote_encrypted);
    EXPECT_TRUE(*files[0]->remote_encrypted);
    EXPECT_EQ("iv-from-index", files[0]->encryption_iv);
    EXPECT_EQ(7u, files[0]->encrypted_with_key_version);
}

TEST(S3CostSafetyTest, RemoteObjectIndexPreservesEncryptionMetadataForSameObjectOnly) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index metadata test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("remote_index_preserve_iv"));
    auto manifest = remoteFile("encrypted/preserve.txt");
    manifest->updated_at = 1'777'777'000;
    manifest->remote_etag = "\"same-etag\"";
    manifest->remote_version_id = "version-a";
    manifest->content_hash = "content-hash";
    manifest->remote_encrypted = true;
    manifest->encryption_iv = "iv-from-manifest";
    manifest->encrypted_with_key_version = 4;
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, manifest, "manifest");

    auto sameFromList = remoteFile("encrypted/preserve.txt");
    sameFromList->updated_at = manifest->updated_at;
    sameFromList->size_bytes = manifest->size_bytes;
    sameFromList->remote_etag = manifest->remote_etag;
    sameFromList->remote_version_id = manifest->remote_version_id;
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, sameFromList, "list_objects_v2");

    auto files = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ("content-hash", files[0]->content_hash.value_or(""));
    ASSERT_TRUE(files[0]->remote_encrypted);
    EXPECT_TRUE(*files[0]->remote_encrypted);
    EXPECT_EQ("iv-from-manifest", files[0]->encryption_iv);
    EXPECT_EQ(4u, files[0]->encrypted_with_key_version);

    auto changedFromList = remoteFile("encrypted/preserve.txt");
    changedFromList->updated_at = manifest->updated_at;
    changedFromList->size_bytes = manifest->size_bytes;
    changedFromList->remote_etag = "\"changed-etag\"";
    changedFromList->remote_version_id = manifest->remote_version_id;
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, changedFromList, "list_objects_v2");

    files = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ASSERT_EQ(1u, files.size());
    EXPECT_FALSE(files[0]->content_hash);
    EXPECT_FALSE(files[0]->remote_encrypted);
    EXPECT_TRUE(files[0]->encryption_iv.empty());
    EXPECT_EQ(0u, files[0]->encrypted_with_key_version);
}

TEST(S3CostSafetyTest, IndexRemoteOnlyPreservesEncryptionMetadataInLocalRow) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index-only test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("index_only_iv"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    auto remote = remoteFile("index-only-encrypted.txt");
    remote->content_hash = "remote-content-hash";
    remote->remote_encrypted = true;
    remote->encryption_iv = "remote-iv";
    remote->encrypted_with_key_version = 5;

    engine->indexAndDeleteFile(remote);

    const auto indexed = vh::db::query::fs::File::getFileByPath(vaultId, "/index-only-encrypted.txt");
    ASSERT_TRUE(indexed);
    ASSERT_TRUE(indexed->content_hash);
    EXPECT_EQ("remote-content-hash", *indexed->content_hash);
    EXPECT_EQ("remote-iv", indexed->encryption_iv);
    EXPECT_EQ(5u, indexed->encrypted_with_key_version);
}

TEST(S3CostSafetyTest, RemoteEncryptedDownloadCreatesBackingParentsAndDecrypts) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed encrypted remote download test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("download_parents"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const std::vector<uint8_t> plaintext{'d', 'o', 'w', 'n', 'l', 'o', 'a', 'd'};
    auto remote = remoteFile("nested/remote/encrypted.png");
    remote->remote_encrypted = true;
    fake->download_payload = engine->encryptionManager->encrypt(plaintext, remote);

    auto task = std::make_shared<vh::sync::Cloud>(engine);
    task->s3Map.emplace(remote->path.u8string(), remote);
    task->ensureDirectoriesFromRemote();

    const auto created = engine->downloadFile(remote);
    ASSERT_TRUE(created);
    EXPECT_TRUE(std::filesystem::exists(created->backing_path.parent_path()));
    EXPECT_TRUE(std::filesystem::exists(created->backing_path));
    EXPECT_EQ(plaintext, engine->decrypt(created));
}

TEST(S3CostSafetyTest, KnownEncryptedRemoteWithoutIvFailsBeforeLocalCreate) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed encrypted remote failure test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("missing_iv"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);
    fake->download_payload = {'c', 'i', 'p', 'h', 'e', 'r'};
    fake->head_response = std::unordered_map<std::string, std::string>{
        {"x-amz-meta-vh-encrypted", "true"},
    };

    auto remote = remoteFile("missing-iv.txt");
    remote->remote_encrypted = true;

    EXPECT_THROW((void)engine->downloadFile(remote), std::runtime_error);
    EXPECT_FALSE(vh::db::query::fs::File::getFileByPath(vaultId, "/missing-iv.txt"));
}

TEST(S3CostSafetyTest, DeleteRemoteDoesNotRequireEncryptionMetadata) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed delete remote test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("delete_no_iv"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    auto remote = remoteFile("delete-without-iv.txt");
    auto op = std::make_shared<vh::sync::model::ScopedOp>();
    vh::sync::tasks::Delete task(engine, remote, op, vh::sync::tasks::Delete::Type::REMOTE);

    task();

    EXPECT_TRUE(op->success);
    EXPECT_EQ(1, fake->delete_object_calls);
    EXPECT_EQ(0, fake->head_object_calls);
    EXPECT_EQ(0, fake->download_to_buffer_calls);
}

TEST(S3CostSafetyTest, PlannerMarksCacheRemoteOnlyAsIndexOnly) {
    auto engine = makePlanningEngine();
    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    auto policy = std::static_pointer_cast<vh::sync::model::RemotePolicy>(engine->sync);
    policy->strategy = vh::sync::model::RemotePolicy::Strategy::Cache;

    const auto remote = remoteFile("remote-only.txt");
    cloud->s3Map.emplace(remote->path.u8string(), remote);

    const auto plan = vh::sync::Planner::build(cloud, policy);

    auto it = std::ranges::find_if(plan, [](const auto& action) {
        return action.type == vh::sync::model::ActionType::IndexRemoteOnly;
    });
    ASSERT_NE(plan.end(), it);
    EXPECT_TRUE(it->freeAfterDownload);

    const auto estimate = vh::sync::Planner::estimateS3Cost(plan);
    EXPECT_EQ(1u, estimate.remote_index_objects);
    EXPECT_EQ(0u, estimate.get_requests);
    EXPECT_EQ(0u, estimate.planned_body_download_bytes);
}

TEST(S3CostSafetyTest, PlannerPreflightsDownloadedByteBudgetBeforeBodyGets) {
    auto engine = makePlanningEngine();
    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    auto policy = std::static_pointer_cast<vh::sync::model::RemotePolicy>(engine->sync);
    policy->strategy = vh::sync::model::RemotePolicy::Strategy::Sync;
    policy->s3_request_budget.max_downloaded_bytes = 41;

    const auto remote = remoteFile("remote-only.txt");
    cloud->s3Map.emplace(remote->path.u8string(), remote);

    EXPECT_THROW(
        (void)vh::sync::Planner::build(cloud, policy),
        vh::storage::s3::RequestBudgetExceeded);
}

TEST(S3CostSafetyTest, BudgetExceededStageMarksEventStalledWithoutGenericError) {
    auto engine = makePlanningEngine();
    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    cloud->event = std::make_shared<vh::sync::model::Event>();
    cloud->runningFlag = true;

    const vh::sync::Stage stages[] = {
        {"budget", [] {
            throw vh::storage::s3::RequestBudgetExceeded("S3 request budget exceeded for DELETE");
        }}
    };

    cloud->runStages(stages);

    EXPECT_EQ(vh::sync::model::Event::Status::STALLED, cloud->event->status);
    EXPECT_EQ("S3 request budget exceeded for DELETE", cloud->event->stall_reason);
    EXPECT_TRUE(cloud->event->error_message.empty());
}

TEST(S3CostSafetyTest, StaleIndexFailureMarksEventStalledWithoutGenericError) {
    auto engine = makePlanningEngine();
    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    cloud->event = std::make_shared<vh::sync::model::Event>();
    cloud->runningFlag = true;

    const vh::sync::Stage stages[] = {
        {"freshness", [] {
            throw vh::sync::model::SyncStalled("remote index is stale and manifest refresh failed");
        }}
    };

    cloud->runStages(stages);

    EXPECT_EQ(vh::sync::model::Event::Status::STALLED, cloud->event->status);
    EXPECT_EQ("remote index is stale and manifest refresh failed", cloud->event->stall_reason);
    EXPECT_TRUE(cloud->event->error_message.empty());
}

TEST(S3CostSafetyTest, RemoteIndexSummaryAppliesMaxAgeFreshnessPolicy) {
    vh::db::query::sync::RemoteIndexSummary summary;
    summary.object_count = 10;
    summary.indexed_at = 1000;

    EXPECT_FALSE(summary.isStale(std::chrono::seconds(60), 1059));
    EXPECT_TRUE(summary.isStale(std::chrono::seconds(60), 1061));
    EXPECT_FALSE(summary.isStale(std::nullopt, 999999));
}

TEST(S3CostSafetyTest, StaleRemoteIndexAfterManifestRefreshFailureStallsWithoutGenericError) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed stale index regression test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("stale_index"));
    vh::db::Transactions::exec("S3CostSafetyTest::seedStaleRemoteIndex", [&](pqxx::work& txn) {
        txn.exec(
            "INSERT INTO remote_object_index "
            "(vault_id, object_key, size_bytes, last_modified, etag, source, indexed_at) "
            "VALUES ($1, $2, $3, CURRENT_TIMESTAMP - INTERVAL '2 hours', $4, $5, CURRENT_TIMESTAMP - INTERVAL '2 hours')",
            pqxx::params{vaultId, "stale.txt", 1, "\"stale\"", "manifest"});
    });

    auto fake = std::make_shared<ManifestRaceS3Controller>();
    auto engine = makeDbBackedCloudEngine(vaultId, fake);
    std::static_pointer_cast<vh::sync::model::RemotePolicy>(engine->sync)->max_remote_index_age = std::chrono::seconds(60);

    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    cloud->event = std::make_shared<vh::sync::model::Event>();
    cloud->runningFlag = true;

    const vh::sync::Stage stages[] = {
        {"initBins", [&] { cloud->initBins(); }}
    };
    cloud->runStages(stages);

    EXPECT_EQ(vh::sync::model::Event::Status::STALLED, cloud->event->status);
    EXPECT_TRUE(cloud->event->error_message.empty());
    EXPECT_NE(std::string::npos, cloud->event->stall_reason.find("remote index is stale and manifest refresh failed"));
    EXPECT_NE(std::string::npos, cloud->event->stall_reason.find("vault sync reconcile"));
}

TEST(S3CostSafetyTest, FirstManifestPublishUsesIfNoneMatchAndReportsConflict) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed manifest publish regression test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("manifest_first"));
    auto fake = std::make_shared<ManifestRaceS3Controller>();
    fake->conditional_failures = {412};
    auto engine = makeDbBackedCloudEngine(vaultId, fake);

    EXPECT_THROW(
        engine->publishRemoteIndexManifest(std::nullopt),
        vh::storage::s3::ConditionalRequestFailed);

    ASSERT_EQ(1u, fake->if_match_values.size());
    EXPECT_FALSE(fake->if_match_values[0].has_value());
    ASSERT_TRUE(fake->if_none_match_values[0].has_value());
    EXPECT_EQ("*", *fake->if_none_match_values[0]);
}

TEST(S3CostSafetyTest, DirectManifestPublishRetriesFirstPublishConflictWithFreshETag) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed manifest retry regression test due to missing environment variables.";
    ensureDbReady();

    for (const auto code : {412, 409}) {
        const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("manifest_first_retry_" + std::to_string(code)));
        auto fake = std::make_shared<ManifestRaceS3Controller>();
        fake->conditional_failures = {code};
        fake->head_responses = {
            std::unordered_map<std::string, std::string>{{"ETag", "\"etag-existing\""}},
            std::unordered_map<std::string, std::string>{{"ETag", "\"etag-after-publish\""}},
        };
        auto engine = makeDbBackedCloudEngine(vaultId, fake);

        EXPECT_NO_THROW(engine->publishRemoteIndexManifestWithRetry());

        ASSERT_EQ(2u, fake->if_match_values.size());
        EXPECT_FALSE(fake->if_match_values[0].has_value());
        ASSERT_TRUE(fake->if_none_match_values[0].has_value());
        EXPECT_EQ("*", *fake->if_none_match_values[0]);
        ASSERT_TRUE(fake->if_match_values[1].has_value());
        EXPECT_EQ("\"etag-existing\"", *fake->if_match_values[1]);
        EXPECT_FALSE(fake->if_none_match_values[1].has_value());
    }
}

TEST(S3CostSafetyTest, ManifestPublishConflictRetriesAfterRefreshingKnownETag) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed manifest retry regression test due to missing environment variables.";
    ensureDbReady();

    for (const auto code : {412, 409}) {
        const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("manifest_retry_" + std::to_string(code)));
        const auto manifest = vh::sync::model::remote_manifest::buildIndexV1(vaultId, {});
        auto fake = std::make_shared<ManifestRaceS3Controller>(manifest);
        fake->conditional_failures = {code};
        fake->head_responses = {
            std::unordered_map<std::string, std::string>{{"ETag", "\"etag-1\""}},
            std::unordered_map<std::string, std::string>{{"ETag", "\"etag-2\""}},
            std::unordered_map<std::string, std::string>{{"ETag", "\"etag-3\""}},
        };
        auto engine = makeDbBackedCloudEngine(vaultId, fake);

        auto file = std::make_shared<vh::fs::model::File>();
        file->path = "/docs/report.txt";
        file->size_bytes = 12;
        file->updated_at = std::time(nullptr);

        const std::vector<vh::sync::model::Action> plan{
            {vh::sync::model::ActionType::Upload, {.rel = u8"docs/report.txt"}, file, nullptr}
        };

        EXPECT_NO_THROW(engine->applyRemoteIndexMutation(plan));

        ASSERT_EQ(2u, fake->if_match_values.size());
        ASSERT_TRUE(fake->if_match_values[0].has_value());
        ASSERT_TRUE(fake->if_match_values[1].has_value());
        EXPECT_EQ("\"etag-1\"", *fake->if_match_values[0]);
        EXPECT_EQ("\"etag-2\"", *fake->if_match_values[1]);
        EXPECT_FALSE(fake->if_none_match_values[0].has_value());
        EXPECT_FALSE(fake->if_none_match_values[1].has_value());
    }
}

TEST(S3CostSafetyTest, BudgetMetricsAfterAsyncFailureMarkEventStalled) {
    struct Case {
        BudgetProbeS3Controller::RequestKind kind;
        vh::storage::s3::S3RequestBudget budget;
        const char* reason;
    };

    std::vector<Case> cases;
    {
        vh::storage::s3::S3RequestBudget budget;
        budget.max_put_requests = 0;
        cases.push_back({BudgetProbeS3Controller::RequestKind::Put, budget, "S3 request budget exceeded for PUT"});
    }
    {
        vh::storage::s3::S3RequestBudget budget;
        budget.max_get_requests = 0;
        cases.push_back({BudgetProbeS3Controller::RequestKind::Get, budget, "S3 request budget exceeded for GET"});
    }
    {
        vh::storage::s3::S3RequestBudget budget;
        budget.max_delete_requests = 0;
        cases.push_back({BudgetProbeS3Controller::RequestKind::Delete, budget, "S3 request budget exceeded for DELETE"});
    }

    for (const auto& c : cases) {
        auto vault = std::make_shared<vh::vault::model::S3Vault>();
        vault->id = 99;
        vault->owner_id = 100;

        auto policy = std::make_shared<vh::sync::model::RemotePolicy>();
        auto fake = std::make_shared<BudgetProbeS3Controller>();
        auto engine = std::make_shared<vh::storage::CloudEngine>();
        engine->vault = vault;
        engine->sync = policy;
        engine->setS3ControllerForTesting(fake);

        fake->setRequestBudget(c.budget);
        try {
            fake->count(c.kind);
        } catch (const vh::storage::s3::RequestBudgetExceeded&) {
        }

        auto cloud = std::make_shared<vh::sync::Cloud>(engine);
        cloud->event = std::make_shared<vh::sync::model::Event>();

        EXPECT_TRUE(cloud->markBudgetExceededIfAny());
        EXPECT_EQ(vh::sync::model::Event::Status::STALLED, cloud->event->status);
        EXPECT_EQ(c.reason, cloud->event->stall_reason);
        EXPECT_TRUE(cloud->event->error_message.empty());
    }
}

TEST(S3CostSafetyTest, SharedStageRemoteDeleteBudgetFailureMarksEventStalled) {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;

    auto policy = std::make_shared<vh::sync::model::RemotePolicy>();
    auto fake = std::make_shared<BudgetProbeS3Controller>();
    auto engine = std::make_shared<vh::storage::CloudEngine>();
    engine->vault = vault;
    engine->sync = policy;
    engine->setS3ControllerForTesting(fake);

    vh::storage::s3::S3RequestBudget budget;
    budget.max_delete_requests = 0;
    fake->setRequestBudget(budget);
    try {
        fake->count(BudgetProbeS3Controller::RequestKind::Delete);
    } catch (const vh::storage::s3::RequestBudgetExceeded&) {
    }

    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    cloud->event = std::make_shared<vh::sync::model::Event>();

    EXPECT_TRUE(cloud->markBudgetExceededIfAny());
    EXPECT_EQ(vh::sync::model::Event::Status::STALLED, cloud->event->status);
    EXPECT_EQ("S3 request budget exceeded for DELETE", cloud->event->stall_reason);
    EXPECT_TRUE(cloud->event->error_message.empty());
}

TEST(S3CostSafetyTest, ScopedBudgetClearsAfterThrownException) {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;

    auto fake = std::make_shared<BudgetProbeS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);

    EXPECT_THROW({
        const vh::storage::ScopedS3RequestBudget guard(
            engine,
            vh::sync::model::s3RequestBudgetForPreset(vh::sync::model::S3BudgetPreset::Conservative));
        throw std::runtime_error("synthetic failure");
    }, std::runtime_error);

    EXPECT_EQ(1, fake->reset_metrics_calls);
    EXPECT_EQ(1, fake->set_budget_calls);
    EXPECT_EQ(1, fake->clear_budget_calls);

    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Get));
}

TEST(S3CostSafetyTest, ScopedBudgetClearsAfterBudgetException) {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;

    auto fake = std::make_shared<BudgetProbeS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);

    vh::storage::s3::S3RequestBudget budget;
    budget.max_get_requests = 0;

    EXPECT_THROW({
        const vh::storage::ScopedS3RequestBudget guard(engine, budget);
        fake->count(BudgetProbeS3Controller::RequestKind::Get);
    }, vh::storage::s3::RequestBudgetExceeded);

    EXPECT_EQ(1, fake->clear_budget_calls);
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Get));
}

TEST(S3CostSafetyTest, RequestBudgetsThrowBeforeNextRequest) {
    auto fake = std::make_shared<BudgetProbeS3Controller>();

    vh::storage::s3::S3RequestBudget budget;
    budget.max_list_requests = 1;
    budget.max_head_requests = 1;
    budget.max_get_requests = 1;
    budget.max_put_requests = 1;
    budget.max_copy_requests = 1;
    budget.max_delete_requests = 1;
    budget.max_downloaded_bytes = 10;
    fake->setRequestBudget(budget);

    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::List));
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Head));
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Get));
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Put));
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Copy));
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Delete));
    EXPECT_NO_THROW(fake->count(BudgetProbeS3Controller::RequestKind::DownloadBytes, 10));

    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::List), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Head), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Get), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Put), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Copy), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::Delete), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_THROW(fake->count(BudgetProbeS3Controller::RequestKind::DownloadBytes, 1), vh::storage::s3::RequestBudgetExceeded);

    const auto metrics = fake->requestMetrics();
    EXPECT_EQ(1u, metrics.list_requests);
    EXPECT_EQ(1u, metrics.head_requests);
    EXPECT_EQ(1u, metrics.get_requests);
    EXPECT_EQ(1u, metrics.put_requests);
    EXPECT_EQ(1u, metrics.copy_requests);
    EXPECT_EQ(1u, metrics.delete_requests);
    EXPECT_EQ(10u, metrics.downloaded_bytes);
}

TEST(S3CostSafetyTest, MultipartUploadCountsInitiatePartsAndComplete) {
    auto fake = std::make_shared<BudgetProbeS3Controller>();

    vh::storage::s3::S3RequestBudget budget;
    budget.max_put_requests = 4;
    fake->setRequestBudget(budget);

    EXPECT_NO_THROW(fake->simulateMultipartPutCounts(2));
    EXPECT_EQ(4u, fake->requestMetrics().put_requests);

    fake->resetRequestMetrics();
    budget.max_put_requests = 3;
    fake->setRequestBudget(budget);
    EXPECT_THROW(fake->simulateMultipartPutCounts(2), vh::storage::s3::RequestBudgetExceeded);
    EXPECT_EQ(3u, fake->requestMetrics().put_requests);
}

TEST(S3CostSafetyTest, PlannerBlocksArchiveBodyDownloads) {
    auto engine = makePlanningEngine();
    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    auto policy = std::static_pointer_cast<vh::sync::model::RemotePolicy>(engine->sync);
    policy->strategy = vh::sync::model::RemotePolicy::Strategy::Sync;

    const auto remote = remoteFile("cold/remote-only.dat", "GLACIER");
    cloud->s3Map.emplace(remote->path.u8string(), remote);

    vh::sync::model::S3CostEstimate notes;
    const auto plan = vh::sync::Planner::build(cloud, policy, &notes);

    const auto downloads = std::ranges::count_if(plan, [](const auto& action) {
        return action.type == vh::sync::model::ActionType::Download;
    });
    EXPECT_EQ(0, downloads);
    EXPECT_EQ(1u, notes.archive_tier_downloads_skipped);
}

TEST(S3CostSafetyTest, EventStoresS3PlanEstimateForDashboard) {
    vh::sync::model::Event event;
    vh::sync::model::S3CostEstimate estimate;
    estimate.list_requests = 1;
    estimate.head_requests = 2;
    estimate.get_requests = 3;
    estimate.put_requests = 4;
    estimate.copy_requests = 5;
    estimate.delete_requests = 6;
    estimate.planned_body_download_bytes = 7;
    estimate.planned_upload_bytes = 8;
    estimate.remote_index_objects = 9;
    estimate.archive_tier_downloads_skipped = 10;

    event.applyS3CostEstimate(estimate);

    EXPECT_EQ(1u, event.s3_estimated_list_requests);
    EXPECT_EQ(2u, event.s3_estimated_head_requests);
    EXPECT_EQ(3u, event.s3_estimated_get_requests);
    EXPECT_EQ(4u, event.s3_estimated_put_requests);
    EXPECT_EQ(5u, event.s3_estimated_copy_requests);
    EXPECT_EQ(6u, event.s3_estimated_delete_requests);
    EXPECT_EQ(7u, event.s3_estimated_body_download_bytes);
    EXPECT_EQ(8u, event.s3_estimated_upload_bytes);
    EXPECT_EQ(9u, event.s3_remote_index_objects);
    EXPECT_EQ(10u, event.s3_archive_downloads_skipped);
}

TEST(S3CostSafetyTest, IndexThroughputDoesNotInflateTransferredBytes) {
    vh::sync::model::Event event;

    auto upload = std::make_unique<vh::sync::model::Throughput>();
    upload->metric_type = vh::sync::model::Throughput::UPLOAD;
    auto uploadOp = upload->newOp();
    uploadOp->size_bytes = 10;
    uploadOp->success = true;

    auto download = std::make_unique<vh::sync::model::Throughput>();
    download->metric_type = vh::sync::model::Throughput::DOWNLOAD;
    auto downloadOp = download->newOp();
    downloadOp->size_bytes = 20;
    downloadOp->success = true;

    auto index = std::make_unique<vh::sync::model::Throughput>();
    index->metric_type = vh::sync::model::Throughput::INDEX;
    auto indexOp = index->newOp();
    indexOp->size_bytes = 1024;
    indexOp->success = true;

    event.throughputs.push_back(std::move(upload));
    event.throughputs.push_back(std::move(download));
    event.throughputs.push_back(std::move(index));

    event.computeDashboardStats();

    EXPECT_EQ(3u, event.num_ops_total);
    EXPECT_EQ(10u, event.bytes_up);
    EXPECT_EQ(20u, event.bytes_down);
}

TEST(S3CostSafetyTest, FilesFromS3XmlParsesStorageClassETagAndRestoreStatus) {
    const std::u8string xml = u8R"XML(
<ListBucketResult>
  <Contents>
    <Key>archive/report.bin</Key>
    <LastModified>2026-01-02T03:04:05.000Z</LastModified>
    <ETag>&quot;abc123&quot;</ETag>
    <Size>123</Size>
    <StorageClass>DEEP_ARCHIVE</StorageClass>
    <RestoreStatus>
      <IsRestoreInProgress>true</IsRestoreInProgress>
    </RestoreStatus>
  </Contents>
</ListBucketResult>
)XML";

    const auto files = vh::fs::model::filesFromS3XML(xml);
    ASSERT_EQ(1u, files.size());
    ASSERT_TRUE(files[0]->remote_storage_class);
    ASSERT_TRUE(files[0]->remote_etag);
    EXPECT_EQ("DEEP_ARCHIVE", *files[0]->remote_storage_class);
    EXPECT_EQ("\"abc123\"", *files[0]->remote_etag);
    ASSERT_TRUE(files[0]->remote_restore_status);
    EXPECT_NE(std::string::npos, files[0]->remote_restore_status->find("IsRestoreInProgress"));
    EXPECT_TRUE(files[0]->requiresArchiveRestoreForBodyGet());
}

TEST(S3CostSafetyTest, FilesFromS3XmlSkipsVaulthallaManifestObjects) {
    const std::u8string xml = u8R"XML(
<ListBucketResult>
  <Contents>
    <Key>.vaulthalla/index-v1.json</Key>
    <LastModified>2026-01-02T03:04:05.000Z</LastModified>
    <ETag>&quot;manifest&quot;</ETag>
    <Size>123</Size>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
  <Contents>
    <Key>data/report.txt</Key>
    <LastModified>2026-01-02T03:04:05.000Z</LastModified>
    <ETag>&quot;data&quot;</ETag>
    <Size>12</Size>
    <StorageClass>STANDARD</StorageClass>
  </Contents>
</ListBucketResult>
)XML";

    const auto files = vh::fs::model::filesFromS3XML(xml);
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ("/data/report.txt", files[0]->path.string());
}

TEST(S3CostSafetyTest, RemoteManifestRoundTripsIndexMetadata) {
    auto standard = remoteFile("docs/readme.txt", "STANDARD");
    standard->remote_etag = "\"etag-readme\"";
    standard->content_hash = "content-hash";
    standard->encryption_iv = "iv";
    standard->encrypted_with_key_version = 7;
    standard->remote_version_id = "version-1";
    standard->remote_sequencer = "000000000000000A";

    auto manifestObject = remoteFile(".vaulthalla/index-v1.json", "STANDARD");
    const std::vector<std::shared_ptr<vh::fs::model::File>> files{standard, manifestObject};

    const auto manifest = vh::sync::model::remote_manifest::buildIndexV1(123, files);
    const auto parsed = vh::sync::model::remote_manifest::parseIndexV1(manifest);

    ASSERT_EQ(1u, parsed.size());
    EXPECT_EQ("/docs/readme.txt", parsed[0]->path.string());
    ASSERT_TRUE(parsed[0]->remote_storage_class);
    EXPECT_EQ("STANDARD", *parsed[0]->remote_storage_class);
    ASSERT_TRUE(parsed[0]->remote_etag);
    EXPECT_EQ("\"etag-readme\"", *parsed[0]->remote_etag);
    ASSERT_TRUE(parsed[0]->content_hash);
    EXPECT_EQ("content-hash", *parsed[0]->content_hash);
    EXPECT_EQ("iv", parsed[0]->encryption_iv);
    EXPECT_EQ(7u, parsed[0]->encrypted_with_key_version);
    ASSERT_TRUE(parsed[0]->remote_version_id);
    EXPECT_EQ("version-1", *parsed[0]->remote_version_id);
    ASSERT_TRUE(parsed[0]->remote_sequencer);
    EXPECT_EQ("000000000000000A", *parsed[0]->remote_sequencer);
}

TEST(S3CostSafetyTest, S3SequencerComparisonRejectsOlderEvents) {
    using vh::db::query::sync::s3SequencerIsNewerOrEqual;
    const auto seq = [](const char* value) { return std::make_optional<std::string>(value); };

    EXPECT_TRUE(s3SequencerIsNewerOrEqual(seq("A"), seq("9")));
    EXPECT_TRUE(s3SequencerIsNewerOrEqual(seq("000A"), seq("A")));
    EXPECT_TRUE(s3SequencerIsNewerOrEqual(seq("10"), seq("F")));
    EXPECT_FALSE(s3SequencerIsNewerOrEqual(seq("9"), seq("A")));
    EXPECT_FALSE(s3SequencerIsNewerOrEqual(seq("F"), seq("10")));
    EXPECT_TRUE(s3SequencerIsNewerOrEqual(std::nullopt, seq("10")));
    EXPECT_TRUE(s3SequencerIsNewerOrEqual(seq("10"), std::nullopt));
}

TEST(S3CostSafetyTest, RemoteManifestValidatesVaultIdCountAndChecksum) {
    auto standard = remoteFile("docs/readme.txt", "STANDARD");
    const std::vector<std::shared_ptr<vh::fs::model::File>> files{standard};

    const auto manifest = vh::sync::model::remote_manifest::buildIndexV1(123, files);
    EXPECT_NO_THROW((void)vh::sync::model::remote_manifest::parseIndexV1(manifest, 123));
    EXPECT_THROW(
        (void)vh::sync::model::remote_manifest::parseIndexV1(manifest, 456),
        std::runtime_error);

    auto parsed = nlohmann::json::parse(manifest);
    parsed["object_count"] = 99;
    EXPECT_THROW(
        (void)vh::sync::model::remote_manifest::parseIndexV1(parsed.dump(), 123),
        std::runtime_error);

    parsed = nlohmann::json::parse(manifest);
    parsed["objects"][0]["size_bytes"] = 999;
    EXPECT_THROW(
        (void)vh::sync::model::remote_manifest::parseIndexV1(parsed.dump(), 123),
        std::runtime_error);
}

TEST(S3CostSafetyTest, SelectedDownloadBlocksIntelligentTieringArchiveStatus) {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;
    vault->encrypt_upstream = true;

    auto fake = std::make_shared<CountingS3Controller>();
    fake->head_response = std::unordered_map<std::string, std::string>{
        {"x-amz-storage-class", "INTELLIGENT_TIERING"},
        {"x-amz-archive-status", "ARCHIVE_ACCESS"},
    };

    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);

    const auto file = remoteFile("archive-tier/object.bin", "INTELLIGENT_TIERING");
    EXPECT_TRUE(engine.selectedDownloadRequiresRestore(file));
}

TEST(S3CostSafetyTest, EncryptedUploadDoesNotDownloadAfterPut) {
    const auto tempDir = std::filesystem::temp_directory_path() / "vh_s3_cost_safety_upload";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);
    const auto backing = tempDir / "ciphertext.bin";
    std::ofstream(backing, std::ios::binary) << "ciphertext";

    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;
    vault->encrypt_upstream = true;

    auto fake = std::make_shared<CountingS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);

    auto file = std::make_shared<vh::fs::model::File>();
    file->path = "/ciphertext.bin";
    file->backing_path = backing;
    file->size_bytes = std::filesystem::file_size(backing);
    file->content_hash = "content-hash";
    file->encryption_iv = "iv";
    file->encrypted_with_key_version = 3;

    engine.upload(file);

    EXPECT_EQ(1, fake->upload_object_with_metadata_calls);
    EXPECT_EQ(0, fake->download_to_buffer_calls);
    EXPECT_EQ("true", fake->last_metadata.at("vh-encrypted"));
    EXPECT_EQ("iv", fake->last_metadata.at("vh-iv"));
    EXPECT_EQ("3", fake->last_metadata.at("vh-key-version"));

    std::filesystem::remove_all(tempDir);
}

TEST(S3CostSafetyTest, EncryptedUploadResolvesRelativeBackingPath) {
    const auto oldBackingPath = vh::paths::backingPath;
    const auto oldMountPath = vh::paths::mountPath;
    struct PathRestore {
        std::filesystem::path backing;
        std::filesystem::path mount;
        ~PathRestore() {
            vh::paths::backingPath = backing;
            vh::paths::mountPath = mount;
        }
    } restore{oldBackingPath, oldMountPath};

    const auto tempDir = std::filesystem::temp_directory_path() / "vh_s3_cost_safety_relative_upload";
    std::filesystem::remove_all(tempDir);
    vh::paths::backingPath = tempDir / "backing";
    vh::paths::mountPath = tempDir / "mount";
    std::filesystem::create_directories(vh::paths::backingPath);
    std::filesystem::create_directories(vh::paths::mountPath);

    const std::filesystem::path relBacking = std::filesystem::path("vault-alias") / "file-alias";
    const auto absBacking = vh::paths::backingPath / relBacking;
    std::filesystem::create_directories(absBacking.parent_path());
    std::ofstream(absBacking, std::ios::binary) << "ciphertext";

    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;
    vault->name = "relative-upload";
    vault->mount_point = "vault-alias";
    vault->encrypt_upstream = true;

    auto fake = std::make_shared<CountingS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.paths = std::make_shared<vh::fs::model::Path>("relative-upload", "vault-alias");
    engine.setS3ControllerForTesting(fake);

    auto file = std::make_shared<vh::fs::model::File>();
    file->path = "/ciphertext.bin";
    file->backing_path = relBacking;
    file->size_bytes = std::filesystem::file_size(absBacking);
    file->content_hash = "content-hash";
    file->encryption_iv = "iv";
    file->encrypted_with_key_version = 3;

    engine.upload(file);

    EXPECT_EQ(1, fake->upload_object_with_metadata_calls);
    EXPECT_EQ(0, fake->download_to_buffer_calls);

    std::filesystem::remove_all(tempDir);
}
