#include "db/encoding/interval.hpp"
#include "db/query/sync/RemoteObjectIndex.hpp"
#include "fs/model/File.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/ScopedS3RequestBudget.hpp"
#include "storage/s3/Controller.hpp"
#include "sync/Cloud.hpp"
#include "sync/Planner.hpp"
#include "sync/model/Event.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemoteManifest.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_map>
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
    int download_to_buffer_calls = 0;
    int upload_object_with_metadata_calls = 0;
    std::unordered_map<std::string, std::string> last_metadata;
    std::optional<std::unordered_map<std::string, std::string>> head_response;

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

    void downloadToBuffer(const std::filesystem::path&, std::vector<uint8_t>&) const override {
        ++const_cast<CountingS3Controller*>(this)->download_to_buffer_calls;
    }

    std::optional<std::unordered_map<std::string, std::string>> getHeadObject(
        const std::filesystem::path&) const override {
        return head_response;
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
