#include "storage/s3/pricing/PriceBotClient.hpp"
#include "storage/s3/pricing/PriceBotModels.hpp"
#include "storage/s3/pricing/PriceBotUsage.hpp"
#include "storage/s3/pricing/PriceProfileResolver.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "sync/model/Action.hpp"
#include "sync/model/Event.hpp"
#include "vault/model/APIKey.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>
#include <queue>
#include <chrono>

namespace {

class FakeTransport final : public vh::storage::s3::pricing::HttpTransport {
public:
    std::queue<vh::storage::s3::pricing::HttpReply> replies;
    std::vector<vh::storage::s3::pricing::HttpRequest> requests;

    vh::storage::s3::pricing::HttpReply perform(
        const vh::storage::s3::pricing::HttpRequest& request,
        std::uint32_t) override {
        requests.push_back(request);
    if (replies.empty()) return {.status = 500, .body = "", .error = ""};
        auto reply = replies.front();
        replies.pop();
        return reply;
    }
};

std::shared_ptr<vh::vault::model::APIKey> priceKey(
    const vh::vault::model::S3Provider provider,
    std::string region = "us-east-1") {
    return std::make_shared<vh::vault::model::APIKey>(
        1,
        "pricing",
        provider,
        "ABCDEFGHIJKLMNOPQRST",
        "ABCDEFGHIJKLMNOPQRSTABCDEFGHIJKLMNOPQRST",
        std::move(region),
        "https://s3.example.com");
}

nlohmann::json minimalProfile(
    const std::string& provider,
    const std::string& region,
    const std::string& storageClass) {
    return {
        {"schema_version", "1.0"},
        {"kind", "vaulthalla.rating_profile"},
        {"profile_id", provider + "/" + region + "/" + storageClass},
        {"catalog_version", "fixture-catalog"},
        {"provider", {{"id", provider}, {"display_name", provider}, {"api_family", "s3-compatible"}}},
        {"scope", {{"region", region}, {"storage_class", storageClass}, {"currency", "USD"}}},
        {"confidence", {{"level", "high"}, {"score", "0.90"}, {"reasons", nlohmann::json::array()}, {"unknowns", nlohmann::json::array()}}},
        {"operation_map", nlohmann::json::object()},
        {"meters", nlohmann::json::object()},
        {"storage_rules", nlohmann::json::array()},
        {"provenance", nlohmann::json::object()},
        {"integrity", {{"content_sha256", ""}, {"signature_alg", "Ed25519"}, {"signature_ref", ""}}}
    };
}

nlohmann::json minimalEstimate(const std::string& cost = "0.01234567") {
    return {
        {"estimated_cost", cost},
        {"currency", "USD"},
        {"breakdown", nlohmann::json::array({{{"meter_key", "request_write"}, {"cost", cost}}})},
        {"free_tier_applied", nlohmann::json::object()},
        {"rounding_applied", nlohmann::json::object()},
        {"confidence", {{"level", "high"}, {"score", "0.90"}, {"reasons", nlohmann::json::array()}, {"unknowns", nlohmann::json::array()}}},
        {"unknowns", nlohmann::json::array({"taxes"})}
    };
}

vh::config::StorageRatesApiConfig testPricingConfig() {
    vh::config::StorageRatesApiConfig cfg;
    cfg.base_url = "http://price-bot.test";
    cfg.timeout_ms = 100;
    cfg.cache_ttl_seconds = 86400;
    cfg.signature_warning_only = true;
    cfg.signature_public_key_path.reset();
    return cfg;
}

void clearPriceCache() {
    std::filesystem::remove_all(vh::paths::getBackingPath() / "price-cache");
}

class PriceCachePathGuard {
public:
    PriceCachePathGuard()
        : oldBackingPath_(vh::paths::backingPath),
          root_(std::filesystem::temp_directory_path() /
              ("vh_s3_pricing_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        vh::paths::backingPath = root_;
        std::filesystem::create_directories(root_);
    }

    ~PriceCachePathGuard() {
        vh::paths::backingPath = oldBackingPath_;
        std::filesystem::remove_all(root_);
    }

private:
    std::filesystem::path oldBackingPath_;
    std::filesystem::path root_;
};

} // namespace

TEST(S3PricingTest, ResolvesProviderTierAndRegionMappings) {
    using vh::vault::model::S3Provider;
    using vh::storage::s3::pricing::resolvePriceProfileTarget;

    const auto aws = vh::storage::s3::provider::resolve(S3Provider::AWS);
    {
        const auto target = resolvePriceProfileTarget(aws, priceKey(S3Provider::AWS), std::nullopt);
        ASSERT_TRUE(target);
        EXPECT_EQ("aws-s3", target->provider);
        EXPECT_EQ("us-east-1", target->region);
        EXPECT_EQ("standard", target->storage_class);
    }
    {
        const auto tier = aws->normalizeStorageTier("standard_ia");
        ASSERT_TRUE(tier.ok) << tier.error;
        const auto target = resolvePriceProfileTarget(aws, priceKey(S3Provider::AWS), tier.resolved);
        ASSERT_TRUE(target);
        EXPECT_EQ("aws-s3", target->provider);
        EXPECT_EQ("us-east-1", target->region);
        EXPECT_EQ("standard-ia", target->storage_class);
    }

    const auto r2 = vh::storage::s3::provider::resolve(S3Provider::CloudflareR2);
    {
        const auto target = resolvePriceProfileTarget(r2, priceKey(S3Provider::CloudflareR2, "auto"), std::nullopt);
        ASSERT_TRUE(target);
        EXPECT_EQ("cloudflare-r2", target->provider);
        EXPECT_EQ("global", target->region);
        EXPECT_EQ("standard", target->storage_class);
    }
    {
        const auto tier = r2->normalizeStorageTier("infrequent_access");
        ASSERT_TRUE(tier.ok) << tier.error;
        const auto target = resolvePriceProfileTarget(r2, priceKey(S3Provider::CloudflareR2, "auto"), tier.resolved);
        ASSERT_TRUE(target);
        EXPECT_EQ("cloudflare-r2", target->provider);
        EXPECT_EQ("global", target->region);
        EXPECT_EQ("infrequent-access", target->storage_class);
    }

    const auto generic = vh::storage::s3::provider::resolve(S3Provider::Other);
    EXPECT_FALSE(resolvePriceProfileTarget(generic, priceKey(S3Provider::Other), std::nullopt));
}

TEST(S3PricingTest, ConvertsS3CostEstimateToUsageInput) {
    vh::sync::model::S3CostEstimate estimate;
    estimate.list_requests = 1;
    estimate.head_requests = 2;
    estimate.get_requests = 3;
    estimate.put_requests = 4;
    estimate.copy_requests = 5;
    estimate.delete_requests = 6;
    estimate.planned_body_download_bytes = 700;
    estimate.planned_upload_bytes = 800;
    estimate.remote_index_objects = 9;

    const auto aws = vh::storage::s3::provider::resolve(vh::vault::model::S3Provider::AWS);
    const auto tier = aws->normalizeStorageTier("standard_ia");
    ASSERT_TRUE(tier.ok) << tier.error;

    const auto usage = vh::storage::s3::pricing::toPriceBotUsageInput(estimate, tier.resolved);
    EXPECT_EQ("1", usage.provider_operation_counts.at("ListObjectsV2"));
    EXPECT_EQ("2", usage.provider_operation_counts.at("HeadObject"));
    EXPECT_EQ("3", usage.provider_operation_counts.at("GetObject"));
    EXPECT_EQ("4", usage.provider_operation_counts.at("PutObject"));
    EXPECT_EQ("5", usage.provider_operation_counts.at("CopyObject"));
    EXPECT_EQ("6", usage.provider_operation_counts.at("DeleteObject"));
    EXPECT_EQ("700", usage.downloaded_bytes);
    EXPECT_EQ("800", usage.uploaded_bytes);
    EXPECT_EQ("700", usage.retrieval_bytes);
    EXPECT_EQ("9", usage.object_count);
}

TEST(S3PricingTest, ParsesProfileAndEstimatePreservingDecimalStrings) {
    const auto profile = vh::storage::s3::pricing::RatingProfile::parse(
        minimalProfile("cloudflare-r2", "global", "standard"));
    EXPECT_EQ("cloudflare-r2/global/standard", profile.profile_id);
    EXPECT_EQ("fixture-catalog", profile.catalog_version);
    EXPECT_EQ("high", profile.confidence_level);

    const auto estimate = vh::storage::s3::pricing::EstimateResult::parse(minimalEstimate("12.34000000"));
    EXPECT_EQ("12.34000000", estimate.estimated_cost);
    EXPECT_EQ("USD", estimate.currency);
    EXPECT_EQ("high", estimate.confidence_level);
    ASSERT_EQ(1u, estimate.unknowns.size());
    EXPECT_EQ("taxes", estimate.unknowns.front());
}

TEST(S3PricingTest, ClientUsesStaleProfileCacheWhenNetworkFails) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = minimalProfile("aws-s3", "us-east-1", "standard").dump(), .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(testPricingConfig(), transport);
    const auto fresh = client.getProfile("aws-s3", "us-east-1", "standard", true);
    ASSERT_TRUE(fresh.ok) << fresh.error;
    EXPECT_FALSE(fresh.stale);

    auto failingTransport = std::make_shared<FakeTransport>();
    failingTransport->replies.push({.status = 500, .body = "{}", .error = ""});
    vh::storage::s3::pricing::PriceBotClient failingClient(testPricingConfig(), failingTransport);
    const auto stale = failingClient.getProfile("aws-s3", "us-east-1", "standard", true);
    ASSERT_TRUE(stale.ok) << stale.error;
    EXPECT_TRUE(stale.stale);
    EXPECT_EQ("aws-s3/us-east-1/standard", stale.value.profile_id);
}

TEST(S3PricingTest, ClientReturnsUnavailableWithoutCacheOnHttpFailure) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 404, .body = R"({"detail":"profile not found"})", .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(testPricingConfig(), transport);
    const auto result = client.getProfile("aws-s3", "us-east-1", "standard", true);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(404, result.http_status);
    EXPECT_NE(std::string::npos, result.error.find("HTTP 404"));
}

TEST(S3PricingTest, ClientUsesStaleEstimateCacheWhenNetworkFails) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    vh::storage::s3::pricing::UsageInput usage;
    usage.provider_operation_counts["PutObject"] = "10";

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = minimalEstimate("0.05000000").dump(), .error = ""});
    vh::storage::s3::pricing::PriceBotClient client(testPricingConfig(), transport);
    const auto fresh = client.estimate("aws-s3", "us-east-1", "standard", usage, true);
    ASSERT_TRUE(fresh.ok) << fresh.error;
    EXPECT_EQ("0.05000000", fresh.value.estimated_cost);

    auto failingTransport = std::make_shared<FakeTransport>();
    failingTransport->replies.push({.status = 0, .body = "", .error = "timeout"});
    vh::storage::s3::pricing::PriceBotClient failingClient(testPricingConfig(), failingTransport);
    const auto stale = failingClient.estimate("aws-s3", "us-east-1", "standard", usage, true);
    ASSERT_TRUE(stale.ok) << stale.error;
    EXPECT_TRUE(stale.stale);
    EXPECT_EQ("0.05000000", stale.value.estimated_cost);
}

TEST(S3PricingTest, EventJsonIncludesPriceEstimateFields) {
    vh::storage::s3::pricing::PriceEstimateReport report;
    report.available = true;
    report.supported = true;
    report.target = {"aws-s3", "us-east-1", "standard"};
    report.estimated_cost = "0.12345678";
    report.currency = "USD";
    report.price_profile_id = "aws-s3/us-east-1/standard";
    report.catalog_version = "20260526T200010Z";
    report.confidence_level = "high";
    report.unknowns = {"taxes"};
    report.breakdown = nlohmann::json::array({{{"meter_key", "request_write"}, {"cost", "0.12345678"}}});

    auto event = std::make_shared<vh::sync::model::Event>();
    event->applyS3PriceEstimate(report);

    nlohmann::json encoded = event;
    EXPECT_EQ("0.12345678", encoded.at("s3_estimated_cost"));
    EXPECT_EQ("USD", encoded.at("s3_estimated_cost_currency"));
    EXPECT_EQ("aws-s3/us-east-1/standard", encoded.at("s3_price_profile_id"));
    EXPECT_EQ("20260526T200010Z", encoded.at("s3_price_catalog_version"));
    EXPECT_EQ("high", encoded.at("s3_price_confidence_level"));
    ASSERT_TRUE(encoded.at("s3_price_unknowns").is_array());
    EXPECT_EQ("taxes", encoded.at("s3_price_unknowns").at(0));
    ASSERT_TRUE(encoded.at("s3_price_breakdown").is_array());
}
