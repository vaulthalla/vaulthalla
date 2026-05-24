#include "db/encoding/interval.hpp"
#include "fs/model/File.hpp"
#include "storage/CloudEngine.hpp"
#include "storage/s3/Controller.hpp"
#include "sync/Cloud.hpp"
#include "sync/Planner.hpp"
#include "sync/model/Policy.hpp"
#include "sync/model/RemoteManifest.hpp"
#include "sync/model/RemotePolicy.hpp"
#include "vault/model/APIKey.hpp"
#include "vault/model/S3Vault.hpp"

#include <gtest/gtest.h>

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
        return action.type == vh::sync::model::ActionType::Download;
    });
    ASSERT_NE(plan.end(), it);
    EXPECT_TRUE(it->freeAfterDownload);
}

TEST(S3CostSafetyTest, PlannerBlocksArchiveBodyDownloads) {
    auto engine = makePlanningEngine();
    auto cloud = std::make_shared<vh::sync::Cloud>(engine);
    auto policy = std::static_pointer_cast<vh::sync::model::RemotePolicy>(engine->sync);
    policy->strategy = vh::sync::model::RemotePolicy::Strategy::Sync;

    const auto remote = remoteFile("cold/remote-only.dat", "GLACIER");
    cloud->s3Map.emplace(remote->path.u8string(), remote);

    const auto plan = vh::sync::Planner::build(cloud, policy);

    const auto downloads = std::ranges::count_if(plan, [](const auto& action) {
        return action.type == vh::sync::model::ActionType::Download;
    });
    EXPECT_EQ(0, downloads);
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
