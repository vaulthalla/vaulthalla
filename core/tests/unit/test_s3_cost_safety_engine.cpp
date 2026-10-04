// S3CostSafetyTest, part 1/4: request-usage capture, engine quotas, S3 budget migration and ws vault budget
// round trip, dry-run index freshness, remote encryption metadata and the remote object index, upload
// storage classes, storage-manager vault lifecycle, gateway/trash deletes, planner and stalled events.

#include "support/s3_cost_safety_fixture.hpp"

namespace vh::test::s3_cost_safety {

TEST(S3CostSafetyTest, ScopedS3RequestUsageCaptureDoesNotCrossContaminateConcurrentThreads) {
    std::barrier start(2);

    auto worker = [&](const std::size_t payloadSize) {
        vh::storage::CloudEngine engine;
        CountingS3Controller controller;
        controller.download_payload.assign(payloadSize, static_cast<uint8_t>('x'));
        start.arrive_and_wait();

        vh::storage::ScopedS3RequestUsageCapture capture(engine);
        std::vector<uint8_t> out;
        controller.downloadToBuffer("object.bin", out);
        return capture.usage();
    };

    auto first = std::async(std::launch::async, worker, 3);
    auto second = std::async(std::launch::async, worker, 11);

    const auto firstUsage = first.get();
    const auto secondUsage = second.get();

    EXPECT_EQ(1, firstUsage.get_requests);
    EXPECT_EQ(3, firstUsage.downloaded_bytes);
    EXPECT_TRUE(firstUsage.touched_upstream);
    EXPECT_EQ(1, secondUsage.get_requests);
    EXPECT_EQ(11, secondUsage.downloaded_bytes);
    EXPECT_TRUE(secondUsage.touched_upstream);
}

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

TEST(S3CostSafetyTest, WsVaultGetAndUpdateRoundTripS3SyncBudget) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed websocket vault update test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("ws_budget"), fake);
    const auto session = superAdminWsSession();

    auto before = vh::protocols::ws::handler::Vaults::get({{"id", vaultId}}, session);
    ASSERT_TRUE(before.contains("vault"));
    ASSERT_TRUE(before["vault"].contains("sync"));
    ASSERT_TRUE(before["vault"]["sync"].contains("s3_request_budget"));

    auto payload = before["vault"];
    payload["name"] = "ws-budget-updated-" + std::to_string(vaultId);
    payload["sync"]["enabled"] = true;
    payload["sync"]["interval"] = 120;
    payload["sync"]["strategy"] = "sync";
    payload["sync"]["conflict_policy"] = "keep_newest";
    payload["sync"]["max_remote_index_age_seconds"] = 60;
    payload["sync"]["s3_request_budget"] = {
        {"list_requests", nullptr},
        {"head_requests", 12},
        {"get_requests", 42},
        {"put_requests", 13},
        {"copy_requests", 14},
        {"delete_requests", 15},
        {"downloaded_bytes", nullptr}
    };

    const auto update = vh::protocols::ws::handler::Vaults::update(payload, session);
    ASSERT_TRUE(update.contains("vault"));
    EXPECT_EQ(payload["name"].get<std::string>(), update["vault"]["name"].get<std::string>());

    const auto persisted = loadRemotePolicyForDbTest(vaultId);
    EXPECT_FALSE(persisted->s3_request_budget.max_list_requests.has_value());
    ASSERT_TRUE(persisted->s3_request_budget.max_get_requests.has_value());
    EXPECT_EQ(42u, *persisted->s3_request_budget.max_get_requests);
    EXPECT_FALSE(persisted->s3_request_budget.max_downloaded_bytes.has_value());
    EXPECT_EQ(120, persisted->interval.count());
    EXPECT_EQ(vh::sync::model::RemotePolicy::Strategy::Sync, persisted->strategy);
    EXPECT_EQ(vh::sync::model::RemotePolicy::ConflictPolicy::KeepNewest, persisted->conflict_policy);
    ASSERT_TRUE(persisted->max_remote_index_age);
    EXPECT_EQ(60, persisted->max_remote_index_age->count());

    const auto after = vh::protocols::ws::handler::Vaults::get({{"id", vaultId}}, session);
    EXPECT_TRUE(after["vault"]["sync"]["s3_request_budget"]["list_requests"].is_null());
    EXPECT_EQ(42u, after["vault"]["sync"]["s3_request_budget"]["get_requests"].get<uint32_t>());
    EXPECT_TRUE(after["vault"]["sync"]["s3_request_budget"]["downloaded_bytes"].is_null());
    EXPECT_EQ(60, after["vault"]["sync"]["max_remote_index_age_seconds"].get<int>());

    const auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->remote_policy()->s3_request_budget.max_get_requests);
    EXPECT_EQ(42u, *engine->remote_policy()->s3_request_budget.max_get_requests);
}

// Stage 0 S6: storage.vault.update only checked that a new api_key_id existed, never APIPermissions::Consume, so a
// caller with vault Edit could bind any API key to a vault. storage.vault.add and CLI `vault update --api-key` both
// require Consume; the gate applies only when the key actually changes.
TEST(S3CostSafetyTest, WsVaultUpdateRequiresConsumeToChangeApiKey) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed ws vault update consume test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto suffix = uniqueSuffix("ws_update_consume");
    const auto vaultId = seedDryRunS3VaultForDbTest(suffix, fake);
    const auto keyIdBefore = [&] {
        return std::static_pointer_cast<vh::vault::model::S3Vault>(
            vh::db::query::vault::Vault::getVault(vaultId))->api_key_id;
    };
    const auto originalKeyId = keyIdBefore();

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    auto otherKey = std::make_shared<vh::vault::model::APIKey>(
        admin->id,
        "consume-gate-key-" + suffix,
        vh::vault::model::S3Provider::AWS,
        "ABCDEFGHIJKLMNOPQRST",
        "ABCDEFGHIJKLMNOPQRSTABCDEFGHIJKLMNOPQRST",
        "us-east-1",
        "https://s3.example.com");
    vh::runtime::Deps::get().apiKeyManager->addAPIKey(otherKey);
    ASSERT_NE(originalKeyId, otherKey->id);

    // Vault editor with no API-key permissions at all.
    const auto editorId = seedS3CostUserForDbTest(suffix, "editor");
    const auto editorSession = wsSessionForUser(editorId, vh::rbac::role::Admin::Custom(
        "ws_update_consume_editor",
        "vault editor without key permissions",
        vh::rbac::permission::admin::Identities::None(),
        vh::rbac::permission::admin::Vaults::Full(),
        vh::rbac::permission::admin::Audits::None(),
        vh::rbac::permission::admin::Settings::None(),
        vh::rbac::permission::admin::Roles::None(),
        vh::rbac::permission::admin::Keys::None(),
        vh::rbac::permission::admin::S3Gateway::None()));

    const auto before = vh::protocols::ws::handler::Vaults::get({{"id", vaultId}}, superAdminWsSession());
    ASSERT_TRUE(before.contains("vault"));

    auto swapKey = before["vault"];
    swapKey["api_key_id"] = otherKey->id;
    try {
        (void)vh::protocols::ws::handler::Vaults::update(swapKey, editorSession);
        ADD_FAILURE() << "vault Edit alone attached an API key the caller cannot consume";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("api-key"), std::string::npos) << e.what();
    }
    EXPECT_EQ(originalKeyId, keyIdBefore());

    // Same key: no Consume needed, so the editor's ordinary edit is not caught by the gate.
    auto sameKey = before["vault"];
    sameKey["description"] = "edited without touching the key";
    try {
        (void)vh::protocols::ws::handler::Vaults::update(sameKey, editorSession);
    } catch (const std::exception& e) {
        EXPECT_EQ(std::string(e.what()).find("api-key"), std::string::npos) << e.what();
    }
    EXPECT_EQ(originalKeyId, keyIdBefore());
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
        const auto userId = insertS3CostHydratableTestUser(
            txn,
            "entry_backing_user_" + suffix,
            "entry-backing-" + suffix + "@vaulthalla.test");

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

TEST(S3CostSafetyTest, ExplicitRemotePlaintextMetadataWinsOverLocalFileIv) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed plaintext remote provenance test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("plain_head_false"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const auto owner = vh::db::query::identities::User::getUserById(engine->vault->owner_id);
    ASSERT_TRUE(owner);

    const std::filesystem::path path = "/plain-head-false.txt";
    const auto local = vh::fs::Filesystem::createFile({
        .path = path,
        .fuse_path = engine->vaultPathToFusePath(path),
        .buffer = {'l', 'o', 'c', 'a', 'l'},
        .engine = engine,
        .user = owner,
        .overwrite = true,
    });
    ASSERT_TRUE(local);
    ASSERT_FALSE(local->encryption_iv.empty());
    ASSERT_GT(local->encrypted_with_key_version, 0u);

    fake->head_response = std::unordered_map<std::string, std::string>{
        {"x-amz-meta-vh-encrypted", "false"},
    };

    const std::vector<uint8_t> plaintext{'p', 'l', 'a', 'i', 'n'};
    const auto decrypted = engine->decryptRemotePayload(
        path,
        plaintext,
        local,
        vh::storage::CloudEngine::RemoteEncryptionResolveOptions{false, true});

    EXPECT_EQ(plaintext, decrypted);
}

TEST(S3CostSafetyTest, ShareStyleRemotePayloadDoesNotTrustLocalFileIv) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed share plaintext provenance test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("share_local_iv"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const std::vector<uint8_t> localPlaintext{'l', 'o', 'c', 'a', 'l'};
    auto local = remoteFile("share/plain-remote.txt");
    (void)engine->encryptionManager->encrypt(localPlaintext, local);
    ASSERT_FALSE(local->encryption_iv.empty());
    ASSERT_GT(local->encrypted_with_key_version, 0u);

    fake->head_response = std::nullopt;

    const std::vector<uint8_t> remotePlaintext{'r', 'e', 'm', 'o', 't', 'e'};
    const auto decrypted = engine->decryptRemotePayload(local->path, remotePlaintext, local, {});

    EXPECT_EQ(remotePlaintext, decrypted);
}

TEST(S3CostSafetyTest, TrustedRemoteFileMetadataDecryptsWhenHeadMetadataMissing) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed trusted remote provenance test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("trusted_remote_iv"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const std::vector<uint8_t> plaintext{'s', 'e', 'c', 'r', 'e', 't'};
    auto remote = remoteFile("trusted/from-index.bin");
    remote->remote_encrypted = true;
    const auto ciphertext = engine->encryptionManager->encrypt(plaintext, remote);
    fake->head_response = std::nullopt;

    const auto decrypted = engine->decryptRemotePayload(
        remote->path,
        ciphertext,
        remote,
        vh::storage::CloudEngine::RemoteEncryptionResolveOptions{true, false});

    EXPECT_EQ(plaintext, decrypted);
    EXPECT_EQ(0, fake->head_object_calls);
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

TEST(S3CostSafetyTest, PlaintextUpstreamUploadMutationDoesNotPoisonRemoteIndexWithLocalIv) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index upload provenance test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("upload_plain_index"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);
    std::static_pointer_cast<vh::vault::model::S3Vault>(engine->vault)->encrypt_upstream = false;

    auto local = remoteFile("uploads/plain.txt");
    local->size_bytes = 5;
    local->updated_at = std::time(nullptr);
    (void)engine->encryptionManager->encrypt({'l', 'o', 'c', 'a', 'l'}, local);
    const auto originalIv = local->encryption_iv;
    const auto originalKeyVersion = local->encrypted_with_key_version;

    const std::vector<vh::sync::model::Action> plan{
        {vh::sync::model::ActionType::Upload, {.rel = u8"uploads/plain.txt"}, local, nullptr}
    };

    ASSERT_NO_THROW(engine->applyRemoteIndexMutation(plan));

    EXPECT_EQ(originalIv, local->encryption_iv);
    EXPECT_EQ(originalKeyVersion, local->encrypted_with_key_version);

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ASSERT_EQ(1u, indexed.size());
    ASSERT_TRUE(indexed[0]->remote_encrypted);
    EXPECT_FALSE(*indexed[0]->remote_encrypted);
    EXPECT_TRUE(indexed[0]->encryption_iv.empty());
    EXPECT_EQ(0u, indexed[0]->encrypted_with_key_version);
    EXPECT_FALSE(indexed[0]->remote_storage_class);
}

TEST(S3CostSafetyTest, EncryptedUpstreamUploadMutationStoresRemoteEncryptionMetadata) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index upload provenance test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("upload_encrypted_index"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);
    std::static_pointer_cast<vh::vault::model::S3Vault>(engine->vault)->encrypt_upstream = true;

    auto local = remoteFile("uploads/encrypted.txt");
    local->size_bytes = 5;
    local->updated_at = std::time(nullptr);
    (void)engine->encryptionManager->encrypt({'l', 'o', 'c', 'a', 'l'}, local);

    const std::vector<vh::sync::model::Action> plan{
        {vh::sync::model::ActionType::Upload, {.rel = u8"uploads/encrypted.txt"}, local, nullptr}
    };

    ASSERT_NO_THROW(engine->applyRemoteIndexMutation(plan));

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ASSERT_EQ(1u, indexed.size());
    ASSERT_TRUE(indexed[0]->remote_encrypted);
    EXPECT_TRUE(*indexed[0]->remote_encrypted);
    EXPECT_EQ(local->encryption_iv, indexed[0]->encryption_iv);
    EXPECT_EQ(local->encrypted_with_key_version, indexed[0]->encrypted_with_key_version);
    EXPECT_FALSE(indexed[0]->remote_storage_class);
}

TEST(S3CostSafetyTest, UploadMutationStoresConfiguredAwsStorageClass) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index storage tier test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("upload_aws_tier_index"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const auto s3Vault = std::static_pointer_cast<vh::vault::model::S3Vault>(engine->vault);
    s3Vault->storage_tier_id = "standard_ia";
    engine->setS3ProviderProfileForTesting(
        vh::storage::s3::provider::resolve(vh::vault::model::S3Provider::AWS));

    auto local = remoteFile("uploads/aws-tier.txt");
    local->size_bytes = 5;
    local->updated_at = std::time(nullptr);
    (void)engine->encryptionManager->encrypt({'l', 'o', 'c', 'a', 'l'}, local);

    const std::vector<vh::sync::model::Action> plan{
        {vh::sync::model::ActionType::Upload, {.rel = u8"uploads/aws-tier.txt"}, local, nullptr}
    };

    ASSERT_NO_THROW(engine->applyRemoteIndexMutation(plan));

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ASSERT_EQ(1u, indexed.size());
    ASSERT_TRUE(indexed[0]->remote_storage_class);
    EXPECT_EQ("STANDARD_IA", *indexed[0]->remote_storage_class);
}

TEST(S3CostSafetyTest, UploadMutationStoresConfiguredR2StorageClass) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed remote index storage tier test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("upload_r2_tier_index"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const auto s3Vault = std::static_pointer_cast<vh::vault::model::S3Vault>(engine->vault);
    s3Vault->storage_tier_id = "infrequent_access";
    engine->setS3ProviderProfileForTesting(
        vh::storage::s3::provider::resolve(vh::vault::model::S3Provider::CloudflareR2));

    auto local = remoteFile("uploads/r2-tier.txt");
    local->size_bytes = 5;
    local->updated_at = std::time(nullptr);
    (void)engine->encryptionManager->encrypt({'l', 'o', 'c', 'a', 'l'}, local);

    const std::vector<vh::sync::model::Action> plan{
        {vh::sync::model::ActionType::Upload, {.rel = u8"uploads/r2-tier.txt"}, local, nullptr}
    };

    ASSERT_NO_THROW(engine->applyRemoteIndexMutation(plan));

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    ASSERT_EQ(1u, indexed.size());
    ASSERT_TRUE(indexed[0]->remote_storage_class);
    EXPECT_EQ("STANDARD_IA", *indexed[0]->remote_storage_class);
}

TEST(S3CostSafetyTest, S3VaultDbRoundTripsStorageTierId) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 vault storage tier test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("vault_tier_roundtrip"), fake);

    auto vault = std::static_pointer_cast<vh::vault::model::S3Vault>(
        vh::db::query::vault::Vault::getVault(vaultId));
    ASSERT_TRUE(vault);
    EXPECT_FALSE(vault->storage_tier_id);

    vault->storage_tier_id = "standard_ia";
    vh::db::query::vault::Vault::upsertVault(vault);

    auto reloaded = std::static_pointer_cast<vh::vault::model::S3Vault>(
        vh::db::query::vault::Vault::getVault(vaultId));
    ASSERT_TRUE(reloaded);
    ASSERT_TRUE(reloaded->storage_tier_id);
    EXPECT_EQ("standard_ia", *reloaded->storage_tier_id);

    reloaded->storage_tier_id = std::nullopt;
    vh::db::query::vault::Vault::upsertVault(reloaded);

    auto cleared = std::static_pointer_cast<vh::vault::model::S3Vault>(
        vh::db::query::vault::Vault::getVault(vaultId));
    ASSERT_TRUE(cleared);
    EXPECT_FALSE(cleared->storage_tier_id);
}

TEST(S3CostSafetyTest, StorageManagerUpdateRemovesOldEnginePathEntry) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed storage manager update test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("manager_update_path"), fake);
    const auto manager = vh::runtime::Deps::get().storageManager;

    const auto originalEngine = manager->getEngine(vaultId);
    ASSERT_TRUE(originalEngine);
    ASSERT_TRUE(originalEngine->paths);
    const auto oldPath = originalEngine->paths->absRelToRoot(
        originalEngine->paths->vaultRoot,
        vh::fs::model::PathType::FUSE_ROOT);

    auto vault = vh::db::query::vault::Vault::getVault(vaultId);
    ASSERT_TRUE(vault);
    // Since vault slugs/fuse names (#95) the FUSE path follows effectiveFuseName(), not the display name,
    // so change the fuse name to force a path change.
    vault->fuse_name = vault->effectiveFuseName() + "-renamed";
    manager->updateVault(vault);

    const auto refreshedEngine = manager->getEngine(vaultId);
    ASSERT_TRUE(refreshedEngine);
    ASSERT_TRUE(refreshedEngine->paths);
    const auto newPath = refreshedEngine->paths->absRelToRoot(
        refreshedEngine->paths->vaultRoot,
        vh::fs::model::PathType::FUSE_ROOT);

    EXPECT_NE(oldPath, newPath);
    EXPECT_NE(originalEngine.get(), refreshedEngine.get());

    const auto engines = manager->getEngines();
    const auto matchingVaults = std::count_if(engines.begin(), engines.end(), [vaultId](const auto& engine) {
        return engine && engine->vault && engine->vault->id == vaultId;
    });
    EXPECT_EQ(1, matchingVaults);
    EXPECT_EQ(nullptr, manager->resolveStorageEngine(oldPath));
    EXPECT_EQ(refreshedEngine, manager->resolveStorageEngine(newPath));
}

TEST(S3CostSafetyTest, StorageManagerRejectsDuplicateVaultNameForSameOwner) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed duplicate vault test due to missing environment variables.";
    ensureSeededRuntimeReady();

    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(owner);
    const auto name = "dup-vault-" + uniqueSuffix("dup");
    const auto makeVault = [&] {
        auto vault = std::make_shared<vh::vault::model::Vault>();
        vault->name = name;
        vault->owner_id = owner->id;
        vault->type = vh::vault::model::VaultType::Local;
        return vault;
    };
    const auto manager = vh::runtime::Deps::get().storageManager;

    const auto first = manager->addVault(makeVault(), std::make_shared<vh::sync::model::LocalPolicy>());
    ASSERT_TRUE(first);
    // Regression: the web console created a second same-named vault (suffixed FUSE name) while the CLI refused.
    EXPECT_THROW(manager->addVault(makeVault(), std::make_shared<vh::sync::model::LocalPolicy>()), std::runtime_error);
    EXPECT_TRUE(vh::db::query::vault::Vault::vaultExists(name, owner->id));

    manager->removeVault(first->id);
}

TEST(S3CostSafetyTest, RegisterUserCreatesOwnedDefaultVault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed registration test due to missing environment variables.";
    ensureSeededRuntimeReady();

    // Regression: web-console registration always failed ("Sync cannot be null on vault creation") after
    // the user row was written, because the default vault was upserted with no owner and no sync policy.
    auto manager = std::make_shared<vh::auth::Manager>();
    auto user = std::make_shared<vh::identities::User>();
    user->name = "reg_" + uniqueSuffix("default_vault");
    user->email = user->name + "@vaulthalla.test";
    user->roles.admin = vh::db::query::rbac::role::Admin::get("unprivileged");
    ASSERT_TRUE(user->roles.admin);

    ASSERT_NO_THROW(manager->registerUser(user, "Zq9#" + uniqueSuffix("pw") + "!Xw-Long"));
    const auto stored = vh::db::query::identities::User::getUserByName(user->name);
    ASSERT_TRUE(stored);
    EXPECT_TRUE(vh::db::query::vault::Vault::vaultExists(user->name + "'s Local Disk Vault", stored->id));
}

TEST(S3CostSafetyTest, DeletingAVaultOwnerDoesNotBreakVaultLoading) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed owner deletion test due to missing environment variables.";
    ensureSeededRuntimeReady();

    // Regression: owner_id is ON DELETE SET NULL; the next listVaults threw "Attempt to convert SQL null to
    // unsigned int" and the daemon could not start after any vault owner was deleted.
    auto manager = std::make_shared<vh::auth::Manager>();
    auto user = std::make_shared<vh::identities::User>();
    user->name = "orphan_" + uniqueSuffix("owner");
    user->email = user->name + "@vaulthalla.test";
    user->roles.admin = vh::db::query::rbac::role::Admin::get("unprivileged");
    ASSERT_TRUE(user->roles.admin);
    manager->registerUser(user, "Zq9#" + uniqueSuffix("pw") + "!Xw-Long");
    const auto stored = vh::db::query::identities::User::getUserByName(user->name);
    ASSERT_TRUE(stored);

    vh::db::query::identities::User::deleteUser(stored->id);

    std::vector<std::shared_ptr<vh::vault::model::Vault>> vaults;
    ASSERT_NO_THROW(vaults = vh::db::query::vault::Vault::listVaults());
    const auto orphan = std::ranges::find_if(vaults, [&](const auto& v) { return v->name == user->name + "'s Local Disk Vault"; });
    ASSERT_NE(orphan, vaults.end());
    EXPECT_EQ((*orphan)->owner_id, 0u);
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

TEST(S3CostSafetyTest, GatewayRemoteDeleteMarksTombstoneWithoutDirectUpstreamDelete) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway remote delete test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto suffix = uniqueSuffix("gateway_delete_nosuchkey");
    const auto bucketName = uniqueBucketName("gateway-delete-nosuchkey");
    const auto vaultId = seedDryRunS3VaultForDbTest(suffix, fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(owner);
    ASSERT_TRUE(owner->isSuperAdmin());

    constexpr std::string_view objectKey = "already-gone.txt";
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "remote_cache",
        .created_by = owner->id
    });
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = vaultId,
        .object_key = std::string(objectKey),
        .etag = "\"gateway-etag\"",
        .size_bytes = 42,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });
    vh::db::query::s3::Gateway::upsertObjectMetadata(vaultId, std::string(objectKey), {{"color", "blue"}});
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remoteFile(std::string(objectKey)), "manifest");

    vh::protocols::s3::ObjectStore store;
    vh::protocols::s3::ResolvedBucket bucket{
        .bucket_name = bucketName,
        .vault_id = vaultId,
        .mode = "remote_cache",
        .api_exclusive = true,
        .engine = engine,
        .actor = owner,
        .gateway_access = std::nullopt
    };

    ASSERT_NO_THROW(store.deleteObject(bucket, std::string(objectKey)));

    EXPECT_EQ(0, fake->delete_object_calls);
    EXPECT_TRUE(fake->deleted_keys.empty());
    EXPECT_FALSE(vh::db::query::s3::Gateway::getObjectState(vaultId, std::string(objectKey)));
    EXPECT_TRUE(vh::db::query::s3::Gateway::listObjectMetadata(vaultId, std::string(objectKey)).empty());
    EXPECT_THROW((void)store.headObject(bucket, std::string(objectKey)), vh::protocols::s3::S3Error);

    const auto listed = store.listObjects(bucket, {});
    EXPECT_TRUE(listed.objects.empty());
    const auto trashed = vh::db::query::fs::File::listTrashedFiles(vaultId);
    EXPECT_TRUE(std::ranges::any_of(trashed, [&](const auto& file) {
        return file && file->path.generic_string() == std::string("/") + std::string(objectKey) &&
            !file->deleted_at.has_value();
    }));

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    EXPECT_TRUE(std::ranges::any_of(indexed, [&](const auto& file) {
        return file && file->path.generic_string() == std::string("/") + std::string(objectKey);
    }));
}

TEST(S3CostSafetyTest, GatewayRemoteDeleteIgnoresDirectUpstreamFailureBecauseSyncOwnsPurge) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway remote delete test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    fake->delete_object_failure = true;
    const auto suffix = uniqueSuffix("gateway_delete_failure");
    const auto bucketName = uniqueBucketName("gateway-delete-failure");
    const auto vaultId = seedDryRunS3VaultForDbTest(suffix, fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const auto owner = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(owner);
    ASSERT_TRUE(owner->isSuperAdmin());

    constexpr std::string_view objectKey = "must-not-local-delete.txt";
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "remote_cache",
        .created_by = owner->id
    });
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = vaultId,
        .object_key = std::string(objectKey),
        .etag = "\"gateway-etag\"",
        .size_bytes = 42,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });
    vh::db::query::s3::Gateway::upsertObjectMetadata(vaultId, std::string(objectKey), {{"color", "red"}});
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remoteFile(std::string(objectKey)), "manifest");

    vh::protocols::s3::ObjectStore store;
    vh::protocols::s3::ResolvedBucket bucket{
        .bucket_name = bucketName,
        .vault_id = vaultId,
        .mode = "remote_cache",
        .api_exclusive = true,
        .engine = engine,
        .actor = owner,
        .gateway_access = std::nullopt
    };

    EXPECT_NO_THROW(store.deleteObject(bucket, std::string(objectKey)));

    EXPECT_EQ(0, fake->delete_object_calls);
    EXPECT_FALSE(vh::db::query::s3::Gateway::getObjectState(vaultId, std::string(objectKey)));
    EXPECT_TRUE(vh::db::query::s3::Gateway::listObjectMetadata(vaultId, std::string(objectKey)).empty());

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    EXPECT_TRUE(std::ranges::any_of(indexed, [&](const auto& file) {
        return file && file->path.generic_string() == std::string("/") + std::string(objectKey);
    }));
}

TEST(S3CostSafetyTest, CloudTrashPurgeDeletesRemoteIndexWithoutRemoteOnlyDownload) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed cloud trash purge test due to missing environment variables.";

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("trash_purge"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);

    const auto owner = vh::db::query::identities::User::getUserById(engine->vault->owner_id);
    ASSERT_TRUE(owner);

    const std::filesystem::path dir = "/Vaulthalla-media";
    const std::filesystem::path path = dir / "foo.txt";
    ASSERT_NO_THROW(engine->mkdir(dir, owner));

    auto created = vh::fs::Filesystem::createFile({
        .path = path,
        .fuse_path = engine->vaultPathToFusePath(path),
        .buffer = {'d', 'e', 'l', 'e', 't', 'e', 'd'},
        .engine = engine,
        .user = owner,
    });
    ASSERT_TRUE(created);

    auto remote = remoteFile("Vaulthalla-media/foo.txt");
    remote->size_bytes = created->size_bytes;
    remote->updated_at = created->updated_at;
    remote->remote_etag = "\"remote-before-delete\"";
    vh::db::query::sync::RemoteObjectIndex::upsertFile(vaultId, remote, "manifest");

    ASSERT_NO_THROW(engine->remove(path, owner->id));
    EXPECT_FALSE(vh::db::query::fs::File::getFileByPath(vaultId, path));
    ASSERT_EQ(1u, vh::db::query::fs::File::listTrashedFiles(vaultId).size());

    auto task = std::make_shared<vh::sync::Cloud>(engine);
    task->startTask();
    task->removeTrashedFiles();
    ASSERT_NE(vh::sync::model::Event::Status::ERROR, task->event->status);
    task->initBins();
    task->sync();
    task->clearBins();

    EXPECT_EQ(1, fake->delete_object_calls);
    ASSERT_EQ(1u, fake->deleted_keys.size());
    EXPECT_EQ("Vaulthalla-media/foo.txt", fake->deleted_keys[0].generic_string());
    EXPECT_EQ(0, fake->download_to_buffer_calls);
    EXPECT_EQ(0u, fake->requestMetrics().get_requests);
    EXPECT_FALSE(vh::db::query::fs::File::getFileByPath(vaultId, path));
    EXPECT_TRUE(vh::db::query::fs::File::listTrashedFiles(vaultId).empty());

    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(vaultId);
    EXPECT_TRUE(std::ranges::none_of(indexed, [&](const auto& file) {
        return file && file->path == path;
    }));
}

TEST(S3CostSafetyTest, RemoveTrashedFileUsesAbsoluteBackingPathDirectly) {
    ScopedPathRoots paths(std::filesystem::temp_directory_path() / "vh_trash_absolute_backing");

    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->id = 2001;
    vault->name = "trash-absolute";
    vault->mount_point = "trash-absolute";

    vh::storage::Engine engine;
    engine.vault = vault;
    engine.paths = std::make_shared<vh::fs::model::Path>("trash-absolute", "trash-absolute");

    const auto backing = engine.paths->backingVaultRoot / "nested" / "file.txt";
    std::filesystem::create_directories(backing.parent_path());
    std::ofstream(backing, std::ios::binary) << "deleted";
    ASSERT_TRUE(std::filesystem::exists(backing));

    auto trashed = std::make_shared<vh::fs::model::file::Trashed>();
    trashed->vault_id = vault->id;
    trashed->path = "/nested/file.txt";
    trashed->backing_path = backing;
    trashed->base32_alias = backing.filename().string();
    trashed->size_bytes = 7;

    engine.removeLocally(trashed);

    EXPECT_FALSE(std::filesystem::exists(backing));
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

TEST(S3CostSafetyTest, TerminalStalledEventStatusSurvivesShutdownStatusParsing) {
    vh::sync::model::Event event;
    event.status = vh::sync::model::Event::Status::STALLED;
    event.stall_reason = "S3 price budget would be exceeded";
    event.timestamp_begin = std::time(nullptr) - 10;
    event.timestamp_end = std::time(nullptr);

    event.parseCurrentStatus();

    EXPECT_EQ(vh::sync::model::Event::Status::STALLED, event.status);
    EXPECT_EQ("S3 price budget would be exceeded", event.stall_reason);
    EXPECT_TRUE(event.error_message.empty());
}

} // namespace vh::test::s3_cost_safety
