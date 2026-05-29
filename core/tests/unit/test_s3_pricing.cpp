#include "storage/s3/pricing/PriceBotClient.hpp"
#include "storage/s3/pricing/PriceEstimate.hpp"
#include "storage/s3/pricing/PriceBotModels.hpp"
#include "storage/s3/pricing/PriceBotUsage.hpp"
#include "storage/s3/pricing/PriceProfileResolver.hpp"
#include "storage/s3/provider/Registry.hpp"
#include "sync/model/Action.hpp"
#include "sync/model/Event.hpp"
#include "vault/model/APIKey.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <paths.h>
#include <queue>
#include <sodium.h>
#include <chrono>
#include <stdexcept>
#include <system_error>
#include <vector>

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

class Ed25519TestKey {
public:
    Ed25519TestKey()
        : publicKeyPath_(std::filesystem::temp_directory_path() /
              ("vh_s3_pricing_ed25519_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pem")) {
        if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed");

        EVP_PKEY_CTX* keygen = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        if (!keygen) throw std::runtime_error("failed to allocate Ed25519 keygen context");
        if (EVP_PKEY_keygen_init(keygen) != 1 || EVP_PKEY_keygen(keygen, &key_) != 1) {
            EVP_PKEY_CTX_free(keygen);
            throw std::runtime_error("failed to generate Ed25519 test key");
        }
        EVP_PKEY_CTX_free(keygen);

        BIO* bio = BIO_new(BIO_s_mem());
        if (!bio) throw std::runtime_error("failed to allocate PEM buffer");
        if (PEM_write_bio_PUBKEY(bio, key_) != 1) {
            BIO_free(bio);
            throw std::runtime_error("failed to encode Ed25519 public key");
        }
        char* data = nullptr;
        const auto len = BIO_get_mem_data(bio, &data);
        std::ofstream output(publicKeyPath_, std::ios::binary | std::ios::trunc);
        output.write(data, len);
        BIO_free(bio);
        if (!output) throw std::runtime_error("failed to write Ed25519 public key");
    }

    ~Ed25519TestKey() {
        if (key_) EVP_PKEY_free(key_);
        std::error_code ec;
        std::filesystem::remove(publicKeyPath_, ec);
    }

    [[nodiscard]] const std::filesystem::path& publicKeyPath() const {
        return publicKeyPath_;
    }

    [[nodiscard]] std::string sign(const std::string& payload) const {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) throw std::runtime_error("failed to allocate Ed25519 signing context");

        size_t signatureLen = 0;
        if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key_) != 1 ||
            EVP_DigestSign(ctx, nullptr, &signatureLen,
                reinterpret_cast<const unsigned char*>(payload.data()), payload.size()) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("failed to size Ed25519 signature");
        }

        std::vector<unsigned char> signature(signatureLen);
        if (EVP_DigestSign(ctx, signature.data(), &signatureLen,
                reinterpret_cast<const unsigned char*>(payload.data()), payload.size()) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("failed to sign payload");
        }
        EVP_MD_CTX_free(ctx);
        signature.resize(signatureLen);

        std::string encoded(sodium_base64_encoded_len(signature.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
        sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            signature.data(),
            signature.size(),
            sodium_base64_VARIANT_ORIGINAL);
        if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
        return encoded;
    }

private:
    EVP_PKEY* key_{nullptr};
    std::filesystem::path publicKeyPath_;
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

nlohmann::json budgetEstimate(const std::string& cost = "0.12345678") {
    auto estimate = minimalEstimate(cost);
    estimate["estimate_mode"] = "budget_conservative";
    estimate["free_tier_policy"] = "ignore_account_wide_free_tiers";
    estimate["free_tiers_applied"] = false;
    return estimate;
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

vh::config::StorageRatesApiConfig strictPricingConfig(const std::filesystem::path& publicKeyPath) {
    auto cfg = testPricingConfig();
    cfg.signature_warning_only = false;
    cfg.signature_public_key_path = publicKeyPath;
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
    EXPECT_TRUE(estimate.estimate_mode.empty());
    EXPECT_TRUE(estimate.free_tier_policy.empty());
    EXPECT_FALSE(estimate.free_tiers_applied.has_value());
    ASSERT_EQ(1u, estimate.unknowns.size());
    EXPECT_EQ("taxes", estimate.unknowns.front());

    const auto budget = vh::storage::s3::pricing::EstimateResult::parse(
        budgetEstimate("13.37000000"));
    EXPECT_EQ("13.37000000", budget.estimated_cost);
    EXPECT_EQ("budget_conservative", budget.estimate_mode);
    EXPECT_EQ("ignore_account_wide_free_tiers", budget.free_tier_policy);
    ASSERT_TRUE(budget.free_tiers_applied.has_value());
    EXPECT_FALSE(*budget.free_tiers_applied);
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

TEST(S3PricingTest, ClientStrictVerificationUsesRawManifestArtifactEndpoint) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    const Ed25519TestKey key;

    const auto manifestBody = nlohmann::json{
        {"schema_version", "1.0"},
        {"kind", "vaulthalla.price_manifest"},
        {"profiles", nlohmann::json::array()}
    }.dump();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = manifestBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(manifestBody) + "\n", .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(strictPricingConfig(key.publicKeyPath()), transport);
    const auto result = client.getManifest(true);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ("vaulthalla.price_manifest", result.value.at("kind"));

    ASSERT_EQ(2u, transport->requests.size());
    EXPECT_EQ("GET", transport->requests[0].method);
    EXPECT_EQ("http://price-bot.test/v1/artifacts/manifest.json", transport->requests[0].url);
    EXPECT_EQ("GET", transport->requests[1].method);
    EXPECT_EQ("http://price-bot.test/v1/artifacts/manifest.json.sig", transport->requests[1].url);
}

TEST(S3PricingTest, ClientStrictVerificationSucceedsForRawProfileAndCachedSignature) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    const Ed25519TestKey key;

    const auto profileBody = minimalProfile("aws-s3", "us-east-1", "standard").dump();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = profileBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(profileBody) + "\n", .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(strictPricingConfig(key.publicKeyPath()), transport);
    const auto fresh = client.getProfile("aws-s3", "us-east-1", "standard", true);
    ASSERT_TRUE(fresh.ok) << fresh.error;
    EXPECT_FALSE(fresh.stale);
    EXPECT_EQ("aws-s3/us-east-1/standard", fresh.value.profile_id);

    ASSERT_EQ(2u, transport->requests.size());
    EXPECT_EQ("http://price-bot.test/v1/artifacts/profiles/aws-s3/us-east-1/standard.json", transport->requests[0].url);
    EXPECT_EQ("http://price-bot.test/v1/artifacts/profiles/aws-s3/us-east-1/standard.json.sig", transport->requests[1].url);

    auto cachedTransport = std::make_shared<FakeTransport>();
    vh::storage::s3::pricing::PriceBotClient cachedClient(strictPricingConfig(key.publicKeyPath()), cachedTransport);
    const auto cached = cachedClient.getProfile("aws-s3", "us-east-1", "standard", false);
    ASSERT_TRUE(cached.ok) << cached.error;
    EXPECT_TRUE(cachedTransport->requests.empty());
    EXPECT_EQ("aws-s3/us-east-1/standard", cached.value.profile_id);
}

TEST(S3PricingTest, ClientStrictVerificationFailsWhenRawProfileBytesAreModified) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    const Ed25519TestKey key;

    const auto signedBody = minimalProfile("aws-s3", "us-east-1", "standard").dump();
    auto modifiedProfile = minimalProfile("aws-s3", "us-east-1", "standard");
    modifiedProfile["catalog_version"] = "tampered-catalog";
    const auto modifiedBody = modifiedProfile.dump();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = modifiedBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(signedBody), .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(strictPricingConfig(key.publicKeyPath()), transport);
    const auto result = client.getProfile("aws-s3", "us-east-1", "standard", true);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ("signature verification failed", result.error);
}

TEST(S3PricingTest, ClientDoesNotTreatEstimateResponsesAsSignedArtifacts) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    const Ed25519TestKey key;
    vh::storage::s3::pricing::UsageInput usage;
    usage.provider_operation_counts["PutObject"] = "10";

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = minimalEstimate("0.06000000").dump(), .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(strictPricingConfig(key.publicKeyPath()), transport);
    const auto result = client.estimate("aws-s3", "us-east-1", "standard", usage, true);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ("0.06000000", result.value.estimated_cost);

    ASSERT_EQ(1u, transport->requests.size());
    EXPECT_EQ("POST", transport->requests[0].method);
    EXPECT_EQ("http://price-bot.test/v1/estimate", transport->requests[0].url);
}

TEST(S3PricingTest, ClientSendsBudgetConservativeEstimateOptions) {
    const PriceCachePathGuard cacheGuard;
    clearPriceCache();
    vh::storage::s3::pricing::UsageInput usage;
    usage.provider_operation_counts["PutObject"] = "10";

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = budgetEstimate("0.06000000").dump(), .error = ""});

    vh::storage::s3::pricing::PriceBotClient client(testPricingConfig(), transport);
    const auto result = client.estimate(
        "aws-s3",
        "us-east-1",
        "standard",
        usage,
        true,
        vh::storage::s3::pricing::PriceEstimateMode::BudgetConservative);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ("budget_conservative", result.value.estimate_mode);
    EXPECT_EQ("ignore_account_wide_free_tiers", result.value.free_tier_policy);
    ASSERT_TRUE(result.value.free_tiers_applied.has_value());
    EXPECT_FALSE(*result.value.free_tiers_applied);

    ASSERT_EQ(1u, transport->requests.size());
    const auto body = nlohmann::json::parse(transport->requests[0].body);
    ASSERT_TRUE(body.contains("options"));
    EXPECT_EQ("budget_conservative", body.at("options").at("mode"));
    EXPECT_FALSE(body.at("options").at("apply_free_tiers"));
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
    report.estimate_mode = "reporting";
    report.free_tier_policy = "apply_free_tiers";
    report.free_tiers_applied = true;
    report.unknowns = {"taxes"};
    report.breakdown = nlohmann::json::array({{{"meter_key", "request_write"}, {"cost", "0.12345678"}}});

    auto event = std::make_shared<vh::sync::model::Event>();
    event->applyS3PriceEstimate(report);

    vh::storage::s3::pricing::PriceEstimateReport budget = report;
    budget.estimated_cost = "1.23000000";
    budget.estimate_mode = "budget_conservative";
    budget.free_tier_policy = "ignore_account_wide_free_tiers";
    budget.free_tiers_applied = false;
    event->applyS3BudgetPriceEstimate(budget);

    nlohmann::json encoded = event;
    EXPECT_EQ("0.12345678", encoded.at("s3_estimated_cost"));
    EXPECT_EQ("USD", encoded.at("s3_estimated_cost_currency"));
    EXPECT_EQ("aws-s3/us-east-1/standard", encoded.at("s3_price_profile_id"));
    EXPECT_EQ("20260526T200010Z", encoded.at("s3_price_catalog_version"));
    EXPECT_EQ("high", encoded.at("s3_price_confidence_level"));
    EXPECT_EQ("reporting", encoded.at("s3_price_estimate_mode"));
    EXPECT_EQ("apply_free_tiers", encoded.at("s3_price_free_tier_policy"));
    EXPECT_TRUE(encoded.at("s3_price_free_tiers_applied"));
    ASSERT_TRUE(encoded.at("s3_price_unknowns").is_array());
    EXPECT_EQ("taxes", encoded.at("s3_price_unknowns").at(0));
    ASSERT_TRUE(encoded.at("s3_price_breakdown").is_array());
    EXPECT_EQ("1.23000000", encoded.at("s3_budget_estimated_cost"));
    EXPECT_EQ("USD", encoded.at("s3_budget_estimated_cost_currency"));
    EXPECT_EQ("budget_conservative", encoded.at("s3_budget_estimate_mode"));
    EXPECT_EQ("ignore_account_wide_free_tiers", encoded.at("s3_budget_free_tier_policy"));
    EXPECT_FALSE(encoded.at("s3_budget_free_tiers_applied"));
}

TEST(S3PricingTest, DryRunFormatShowsEstimateModeAndFreeTierPolicy) {
    vh::storage::s3::pricing::PriceEstimateReport report;
    report.available = true;
    report.supported = true;
    report.target = {"cloudflare-r2", "global", "standard"};
    report.estimated_cost = "0.00000000";
    report.currency = "USD";
    report.price_profile_id = "cloudflare-r2/global/standard";
    report.catalog_version = "fixture";
    report.confidence_level = "high";
    report.estimate_mode = "budget_conservative";
    report.free_tier_policy = "ignore_account_wide_free_tiers";
    report.free_tiers_applied = false;

    const auto output = vh::storage::s3::pricing::formatPriceEstimateForDryRun(
        report,
        "Budget-safe estimate");
    EXPECT_NE(std::string::npos, output.find("Budget-safe estimate"));
    EXPECT_NE(std::string::npos, output.find("Mode: budget_conservative"));
    EXPECT_NE(std::string::npos, output.find("Free tier policy: ignore_account_wide_free_tiers"));
    EXPECT_NE(std::string::npos, output.find("Free tiers applied: no"));
}
