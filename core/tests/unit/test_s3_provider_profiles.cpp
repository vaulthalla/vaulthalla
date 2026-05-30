#include "storage/s3/Controller.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "vault/model/S3Vault.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

std::shared_ptr<vh::vault::model::APIKey> providerProfileDummyApiKey() {
    return std::make_shared<vh::vault::model::APIKey>(
        1,
        "unit",
        vh::vault::model::S3Provider::AWS,
        "ABCDEFGHIJKLMNOPQRST",
        "ABCDEFGHIJKLMNOPQRSTABCDEFGHIJKLMNOPQRST",
        "us-east-1",
        "https://s3.example.com");
}

class HeaderProbeController final : public vh::storage::s3::Controller {
public:
    HeaderProbeController()
        : Controller(providerProfileDummyApiKey(), "unit-bucket") {}

    std::map<std::string, std::string> headersFor(
        const vh::storage::s3::RequestOptions& options,
        const bool includeMetadata = true) const {
        auto headers = buildHeaderMap("UNSIGNED-PAYLOAD");
        applyRequestOptions(headers, options, includeMetadata);
        return headers;
    }
};

void expectClears(const vh::storage::s3::provider::ProfilePtr& profile, const std::string& value) {
    const auto tier = profile->normalizeStorageTier(value);
    ASSERT_TRUE(tier.ok) << tier.error;
    EXPECT_FALSE(tier.normalized_id);
    EXPECT_FALSE(tier.resolved);
}

} // namespace

TEST(S3ProviderProfilesTest, RegistryMapsFirstClassAndGenericProviders) {
    using vh::vault::model::S3Provider;
    using vh::storage::s3::provider::SupportLevel;

    EXPECT_EQ("aws-s3", vh::storage::s3::provider::resolve(S3Provider::AWS)->id());
    EXPECT_EQ(SupportLevel::FirstClass, vh::storage::s3::provider::resolve(S3Provider::AWS)->supportLevel());

    EXPECT_EQ("cloudflare-r2", vh::storage::s3::provider::resolve(S3Provider::CloudflareR2)->id());
    EXPECT_EQ(SupportLevel::FirstClass, vh::storage::s3::provider::resolve(S3Provider::CloudflareR2)->supportLevel());

    for (const auto provider : {
             S3Provider::Wasabi,
             S3Provider::BackblazeB2,
             S3Provider::DigitalOcean,
             S3Provider::MinIO,
             S3Provider::Ceph,
             S3Provider::Storj,
             S3Provider::Other,
         }) {
        const auto profile = vh::storage::s3::provider::resolve(provider);
        EXPECT_EQ("generic-s3-compatible", profile->id());
        EXPECT_EQ(SupportLevel::Generic, profile->supportLevel());
    }
}

TEST(S3ProviderProfilesTest, AwsNormalizesSelectableTiersAndRejectsInvalidValues) {
    using vh::vault::model::S3Provider;
    const auto aws = vh::storage::s3::provider::resolve(S3Provider::AWS);

    for (const auto& value : {"standard", "STANDARD"}) {
        const auto tier = aws->normalizeStorageTier(value);
        ASSERT_TRUE(tier.ok) << tier.error;
        ASSERT_TRUE(tier.normalized_id);
        EXPECT_EQ("standard", *tier.normalized_id);
        ASSERT_TRUE(tier.resolved);
        EXPECT_EQ("STANDARD", *tier.resolved->wire_class);
    }

    for (const auto& value : {"standard_ia", "STANDARD_IA", "standard-ia"}) {
        const auto tier = aws->normalizeStorageTier(value);
        ASSERT_TRUE(tier.ok) << tier.error;
        ASSERT_TRUE(tier.normalized_id);
        EXPECT_EQ("standard_ia", *tier.normalized_id);
        ASSERT_TRUE(tier.resolved);
        EXPECT_EQ("STANDARD_IA", *tier.resolved->wire_class);
    }

    expectClears(aws, "none");
    expectClears(aws, "default");
    expectClears(aws, "provider-default");
    expectClears(aws, " ");
    EXPECT_FALSE(aws->normalizeStorageTier(std::string{"bad\nvalue"}).ok);
    EXPECT_FALSE(aws->normalizeStorageTier(std::string{"\n"}).ok);

    const auto deepArchive = aws->normalizeStorageTier("deep_archive");
    EXPECT_FALSE(deepArchive.ok);
    EXPECT_EQ(
        "storage tier 'deep_archive' is not selectable for AWS S3 vault defaults yet",
        deepArchive.error);
}

TEST(S3ProviderProfilesTest, R2NormalizesSelectableTiers) {
    using vh::vault::model::S3Provider;
    const auto r2 = vh::storage::s3::provider::resolve(S3Provider::CloudflareR2);

    const auto standard = r2->normalizeStorageTier("standard");
    ASSERT_TRUE(standard.ok) << standard.error;
    ASSERT_TRUE(standard.normalized_id);
    EXPECT_EQ("standard", *standard.normalized_id);
    ASSERT_TRUE(standard.resolved);
    EXPECT_EQ("STANDARD", *standard.resolved->wire_class);

    for (const auto& value : {"infrequent_access", "infrequent-access", "STANDARD_IA", "standard-ia"}) {
        const auto tier = r2->normalizeStorageTier(value);
        ASSERT_TRUE(tier.ok) << tier.error;
        ASSERT_TRUE(tier.normalized_id);
        EXPECT_EQ("infrequent_access", *tier.normalized_id);
        ASSERT_TRUE(tier.resolved);
        EXPECT_EQ("STANDARD_IA", *tier.resolved->wire_class);
    }
}

TEST(S3ProviderProfilesTest, GenericRejectsConfiguredTier) {
    using vh::vault::model::S3Provider;
    const auto generic = vh::storage::s3::provider::resolve(S3Provider::BackblazeB2);

    expectClears(generic, "none");

    const auto tier = generic->normalizeStorageTier("standard_ia");
    EXPECT_FALSE(tier.ok);
    EXPECT_EQ(
        "storage tier selection requires a first-class S3 provider profile; provider Backblaze B2 is running in generic S3-compatible mode",
        tier.error);
}

TEST(S3ProviderProfilesTest, VaultJsonRoundTripsStorageTier) {
    vh::vault::model::S3Vault vault;
    vault.id = 42;
    vault.name = "s3";
    vault.type = vh::vault::model::VaultType::S3;
    vault.owner_id = 7;
    vault.mount_point = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    vault.created_at = 1700000000;
    vault.api_key_id = 9;
    vault.bucket = "bucket";
    vault.storage_tier_id = "standard_ia";

    nlohmann::json encoded = vault;
    ASSERT_TRUE(encoded.contains("storage_tier_id"));
    EXPECT_EQ("standard_ia", encoded.at("storage_tier_id"));

    const auto decoded = encoded.get<vh::vault::model::S3Vault>();
    ASSERT_TRUE(decoded.storage_tier_id);
    EXPECT_EQ("standard_ia", *decoded.storage_tier_id);

    encoded.erase("storage_tier_id");
    const auto missing = encoded.get<vh::vault::model::S3Vault>();
    EXPECT_FALSE(missing.storage_tier_id);

    encoded["storage_tier_id"] = nullptr;
    const auto cleared = encoded.get<vh::vault::model::S3Vault>();
    EXPECT_FALSE(cleared.storage_tier_id);
}

TEST(S3ProviderProfilesTest, RequestMutationUsesSignedSystemHeaderNotMetadata) {
    using vh::storage::s3::provider::RequestOperation;
    using vh::vault::model::S3Provider;

    const auto aws = vh::storage::s3::provider::resolve(S3Provider::AWS);
    const auto tier = aws->normalizeStorageTier("standard_ia");
    ASSERT_TRUE(tier.ok) << tier.error;
    ASSERT_TRUE(tier.resolved);

    for (const auto operation : {
             RequestOperation::PutObject,
             RequestOperation::CreateMultipartUpload,
             RequestOperation::CopyObjectRewrite,
         }) {
        const auto mutation = aws->requestMutation(operation, tier.resolved);
        ASSERT_TRUE(mutation.system_headers.contains("x-amz-storage-class"));
        EXPECT_EQ("STANDARD_IA", mutation.system_headers.at("x-amz-storage-class"));
    }

    EXPECT_TRUE(aws->requestMutation(RequestOperation::UploadPart, tier.resolved).system_headers.empty());
    EXPECT_TRUE(aws->requestMutation(RequestOperation::CompleteMultipartUpload, tier.resolved).system_headers.empty());
    EXPECT_TRUE(aws->requestMutation(RequestOperation::PutObject, std::nullopt).system_headers.empty());

    vh::storage::s3::RequestOptions options;
    options.system_headers = aws->requestMutation(RequestOperation::PutObject, tier.resolved).system_headers;
    options.metadata = {{"storage-class", "SHOULD_BE_USER_METADATA_ONLY"}};

    const HeaderProbeController controller;
    const auto headers = controller.headersFor(options);
    ASSERT_TRUE(headers.contains("x-amz-storage-class"));
    EXPECT_EQ("STANDARD_IA", headers.at("x-amz-storage-class"));
    ASSERT_TRUE(headers.contains("x-amz-meta-storage-class"));
    EXPECT_EQ("SHOULD_BE_USER_METADATA_ONLY", headers.at("x-amz-meta-storage-class"));
}

TEST(S3ProviderProfilesTest, CopyRewriteSystemHeaderCanExcludeNewMetadata) {
    using vh::storage::s3::provider::RequestOperation;
    using vh::vault::model::S3Provider;

    const auto r2 = vh::storage::s3::provider::resolve(S3Provider::CloudflareR2);
    const auto tier = r2->normalizeStorageTier("infrequent_access");
    ASSERT_TRUE(tier.ok) << tier.error;
    ASSERT_TRUE(tier.resolved);

    vh::storage::s3::RequestOptions options;
    options.system_headers = r2->requestMutation(RequestOperation::CopyObjectRewrite, tier.resolved).system_headers;
    options.metadata = {{"storage-class", "not-a-system-header"}};

    const HeaderProbeController controller;
    const auto headers = controller.headersFor(options, false);
    ASSERT_TRUE(headers.contains("x-amz-storage-class"));
    EXPECT_EQ("STANDARD_IA", headers.at("x-amz-storage-class"));
    EXPECT_FALSE(headers.contains("x-amz-meta-storage-class"));
}
