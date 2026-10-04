// S3CostSafetyTest, part 4/4: stale/overridden price-budget reservations, vault pricing stats, remote index
// and manifest freshness/publish races, scoped request budgets, S3 listing/manifest parsing, uploads.

#include "support/s3_cost_safety_fixture.hpp"

namespace vh::test::s3_cost_safety {

TEST(S3CostSafetyTest, PriceBudgetStaleReservationsExpireSafely) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed price budget stale reservation test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("budget_stale"));
    saveVaultBudgetPolicyForDbTest(
        vaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "10.00000000");

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto decision = service.preflight({
        .vault_id = vaultId,
        .run_uuid = uniqueSuffix("stale-reservation"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });
    ASSERT_FALSE(decision.reservations.empty());
    const auto reservationId = decision.reservations.front().id;

    vh::db::Transactions::exec("S3CostSafetyTest::agePriceBudgetReservation", [&](pqxx::work& txn) {
        txn.exec(
            "UPDATE s3_price_budget_ledger "
            "SET created_at = CURRENT_TIMESTAMP - interval '25 hours' "
            "WHERE id = $1",
            pqxx::params{reservationId});
    });

    service.expireStaleReservations();

    const auto status = vh::db::Transactions::exec("S3CostSafetyTest::priceBudgetReservationStatus", [&](pqxx::work& txn) {
        return txn.exec(
            "SELECT status FROM s3_price_budget_ledger WHERE id = $1",
            pqxx::params{reservationId}).one_field().as<std::string>();
    });
    EXPECT_EQ("expired", status);
}

TEST(S3CostSafetyTest, PriceBudgetOverrideRequiresExactVaultRunPolicySet) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed price budget override test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("budget_override_exact"));
    const auto ownerId = ownerForVaultDbTest(vaultId);
    const auto basePolicy = saveVaultBudgetPolicyForDbTest(
        vaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000");
    const auto providerPolicy = saveVaultBudgetPolicyForDbTest(
        vaultId,
        "aws-s3",
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000");

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto runUuid = uniqueSuffix("override-run");
    const auto requested = service.requestOverride({
        .run_uuid = runUuid,
        .vault_id = vaultId,
        .requested_by = ownerId,
        .reason = "release readiness exact policy set test",
        .policy_ids = {basePolicy.id, providerPolicy.id},
        .estimated_cost = "1.00000000",
        .currency = "USD",
        .ttl_minutes = 30
    });
    const auto approved = service.approveOverride(requested.id, ownerId);
    ASSERT_EQ("approved", approved.status);

    EXPECT_FALSE(service.consumeApprovedOverride(vaultId, {basePolicy.id}, runUuid));

    const auto consumed = service.consumeApprovedOverride(vaultId, {basePolicy.id, providerPolicy.id}, runUuid);
    ASSERT_TRUE(consumed);
    EXPECT_EQ(requested.id, consumed->id);
    EXPECT_EQ("used", consumed->status);

    EXPECT_FALSE(service.consumeApprovedOverride(vaultId, {basePolicy.id, providerPolicy.id}, runUuid));
}

TEST(S3CostSafetyTest, VaultPricingDashboardStatsOnlyUseApplicableProviderScopeAndVaultLedgerRows) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed pricing dashboard scope test due to missing environment variables.";
    ensureDbReady();

    const auto awsVaultId = seedS3VaultForDbTest(uniqueSuffix("pricing_aws"));
    const auto r2VaultId = seedS3VaultForDbTest(uniqueSuffix("pricing_r2"));
    const auto localVaultId = vh::db::Transactions::exec("S3CostSafetyTest::seedLocalVaultForPricingStats", [&](pqxx::work& txn) {
        const auto userId = insertS3CostHydratableTestUser(
            txn,
            "local-pricing-user",
            uniqueSuffix("local-pricing") + "@vaulthalla.test");
        const auto vaultId = txn.exec(
            "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
            pqxx::params{"local", uniqueSuffix("Local Pricing"), userId, "ABCDEFGHJKMNPQRSTVWXYZ0123456789", ""})
            .one_field().as<std::uint32_t>();
        txn.exec(
            "WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
            "INSERT INTO fsync (sync_id, conflict_policy) SELECT id, 'keep_both' FROM ins",
            pqxx::params{vaultId});
        return vaultId;
    });
    attachS3ProviderForDbTest(awsVaultId, "AWS");
    attachS3ProviderForDbTest(r2VaultId, "Cloudflare R2");

    vh::storage::s3::pricing::PriceBudgetPolicy awsProviderPolicy;
    awsProviderPolicy.scope = vh::storage::s3::pricing::PriceBudgetScope::Provider;
    awsProviderPolicy.provider_key = "aws-s3";
    awsProviderPolicy.mode = vh::storage::s3::pricing::PriceBudgetMode::Report;
    awsProviderPolicy.currency = "USD";
    awsProviderPolicy.max_monthly_cost = "10.00000000";
    awsProviderPolicy = vh::storage::s3::pricing::PriceBudgetService{}.upsertPolicy(std::move(awsProviderPolicy));

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto awsDecision = service.preflight({
        .vault_id = awsVaultId,
        .run_uuid = uniqueSuffix("aws-ledger"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("3.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });
    ASSERT_FALSE(awsDecision.reservations.empty());
    service.commit(awsDecision.reservations, std::make_optional<std::string>("3.00000000"));

    const auto r2Stats = service.dashboardStats(r2VaultId);
    EXPECT_EQ(0u, r2Stats.active_policies);
    EXPECT_EQ("0.00000000", r2Stats.current_monthly_spend);
    EXPECT_TRUE(r2Stats.trends.empty());

    const auto localStats = service.dashboardStats(localVaultId);
    EXPECT_EQ(0u, localStats.active_policies);
    EXPECT_EQ("0.00000000", localStats.current_monthly_spend);
    EXPECT_TRUE(localStats.trends.empty());
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

TEST(S3CostSafetyTest, PlaintextUpstreamEmptyUploadWritesZeroByteRemoteObject) {
    const auto tempDir = std::filesystem::temp_directory_path() / "vh_s3_plain_empty_upload";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);
    const auto backing = tempDir / "empty.bin";
    std::ofstream(backing, std::ios::binary | std::ios::trunc).close();

    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;
    vault->encrypt_upstream = false;

    auto fake = std::make_shared<CountingS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);

    auto file = std::make_shared<vh::fs::model::File>();
    file->path = "/empty.txt";
    file->backing_path = backing;
    file->size_bytes = 0;
    file->content_hash = "empty-content-hash";

    engine.upload(file);

    EXPECT_EQ(1, fake->upload_buffer_with_metadata_calls);
    EXPECT_EQ("empty.txt", fake->last_uploaded_key.generic_string());
    EXPECT_EQ(0u, fake->last_uploaded_buffer_size);
    EXPECT_EQ("false", fake->last_metadata.at("vh-encrypted"));
    EXPECT_EQ("empty-content-hash", fake->last_metadata.at("content-hash"));

    std::filesystem::remove_all(tempDir);
}

TEST(S3CostSafetyTest, PlaintextUploadBufferObjectWritesDirectoryMarkerKey) {
    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;
    vault->encrypt_upstream = false;

    auto fake = std::make_shared<CountingS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);

    const auto file = engine.uploadBufferObject("/folder/", {}, "marker-content-hash");

    EXPECT_EQ(1, fake->upload_buffer_with_metadata_calls);
    EXPECT_EQ("folder/", fake->last_uploaded_key.generic_string());
    EXPECT_EQ(0u, fake->last_uploaded_buffer_size);
    ASSERT_TRUE(file);
    EXPECT_EQ("/folder/", file->path.generic_string());
    EXPECT_EQ(0u, file->size_bytes);
    ASSERT_TRUE(file->remote_encrypted);
    EXPECT_FALSE(*file->remote_encrypted);
    ASSERT_TRUE(file->content_hash);
    EXPECT_EQ("marker-content-hash", *file->content_hash);
    EXPECT_EQ("false", fake->last_metadata.at("vh-encrypted"));
    EXPECT_EQ("marker-content-hash", fake->last_metadata.at("content-hash"));
}

TEST(S3CostSafetyTest, EncryptedUpstreamEmptyUploadStoresEncryptedPayloadMetadata) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed empty encrypted upload test due to missing environment variables.";

    const auto tempDir = std::filesystem::temp_directory_path() / "vh_s3_encrypted_empty_upload";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);
    const auto backing = tempDir / "empty.bin";
    std::ofstream(backing, std::ios::binary | std::ios::trunc).close();

    auto fake = std::make_shared<CountingS3Controller>();
    const auto vaultId = seedDryRunS3VaultForDbTest(uniqueSuffix("empty_encrypted_upload"), fake);
    auto engine = std::static_pointer_cast<vh::storage::CloudEngine>(
        vh::runtime::Deps::get().storageManager->getEngine(vaultId));
    engine->setS3ControllerForTesting(fake);
    std::static_pointer_cast<vh::vault::model::S3Vault>(engine->vault)->encrypt_upstream = true;

    auto file = std::make_shared<vh::fs::model::File>();
    file->path = "/empty.txt";
    file->backing_path = backing;
    file->size_bytes = 0;
    file->content_hash = "empty-content-hash";

    engine->upload(file);

    EXPECT_EQ(1, fake->upload_buffer_with_metadata_calls);
    EXPECT_EQ("empty.txt", fake->last_uploaded_key.generic_string());
    EXPECT_GT(fake->last_uploaded_buffer_size, 0u);
    EXPECT_EQ("true", fake->last_metadata.at("vh-encrypted"));
    ASSERT_FALSE(file->encryption_iv.empty());
    EXPECT_EQ(file->encryption_iv, fake->last_metadata.at("vh-iv"));
    EXPECT_GT(file->encrypted_with_key_version, 0u);
    EXPECT_EQ(std::to_string(file->encrypted_with_key_version), fake->last_metadata.at("vh-key-version"));

    std::filesystem::remove_all(tempDir);
}

TEST(S3CostSafetyTest, EncryptedUploadPassesConfiguredStorageClassAsSystemHeader) {
    const auto tempDir = std::filesystem::temp_directory_path() / "vh_s3_storage_tier_upload";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);
    const auto backing = tempDir / "ciphertext.bin";
    std::ofstream(backing, std::ios::binary) << "ciphertext";

    auto vault = std::make_shared<vh::vault::model::S3Vault>();
    vault->id = 99;
    vault->owner_id = 100;
    vault->encrypt_upstream = true;
    vault->storage_tier_id = "standard_ia";

    auto fake = std::make_shared<CountingS3Controller>();
    vh::storage::CloudEngine engine;
    engine.vault = vault;
    engine.setS3ControllerForTesting(fake);
    engine.setS3ProviderProfileForTesting(
        vh::storage::s3::provider::resolve(vh::vault::model::S3Provider::AWS));

    auto file = std::make_shared<vh::fs::model::File>();
    file->path = "/ciphertext.bin";
    file->backing_path = backing;
    file->size_bytes = std::filesystem::file_size(backing);
    file->content_hash = "content-hash";
    file->encryption_iv = "iv";
    file->encrypted_with_key_version = 3;

    engine.upload(file);

    EXPECT_EQ(1, fake->upload_object_with_metadata_calls);
    ASSERT_TRUE(fake->last_system_headers.contains("x-amz-storage-class"));
    EXPECT_EQ("STANDARD_IA", fake->last_system_headers.at("x-amz-storage-class"));
    EXPECT_FALSE(fake->last_metadata.contains("storage-class"));

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

} // namespace vh::test::s3_cost_safety
