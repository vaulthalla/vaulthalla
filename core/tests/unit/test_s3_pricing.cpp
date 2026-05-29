#include "storage/s3/pricing/PriceBotClient.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"
#include "storage/s3/pricing/PriceEstimate.hpp"
#include "storage/s3/pricing/PriceBotModels.hpp"
#include "storage/s3/pricing/PriceBotUsage.hpp"
#include "storage/s3/pricing/LocalEstimator.hpp"
#include "storage/s3/pricing/PriceCatalogStore.hpp"
#include "storage/s3/curl/helpers.hpp"
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

nlohmann::json meter(
    const std::string& meterKey,
    const std::string& meterType,
    const std::string& unit,
    const std::string& billingUnit,
    const std::string& rate,
    const std::string& rateUnit,
    const std::string& roundingRule = "none",
    const std::optional<std::string>& freeTierAmount = std::nullopt,
    const std::optional<std::string>& freeTierUnit = std::nullopt,
    nlohmann::json rules = nlohmann::json::object()) {
    nlohmann::json out = {
        {"meter_key", meterKey},
        {"meter_type", meterType},
        {"unit", unit},
        {"billing_unit", billingUnit},
        {"rounding_rule", roundingRule},
        {"free_tier_scope", freeTierAmount ? "account_month" : "none"},
        {"rules", std::move(rules)},
        {"tiers", nlohmann::json::array({{
            {"rate", rate},
            {"rate_unit", rateUnit},
            {"tier_start", "0"}
        }})}
    };
    if (freeTierAmount) out["free_tier_amount"] = *freeTierAmount;
    if (freeTierUnit) out["free_tier_unit"] = *freeTierUnit;
    return out;
}

nlohmann::json opMap(const std::string& meterKey, const std::string& multiplier = "1") {
    return {{"meter_key", meterKey}, {"multiplier", multiplier}, {"rules", nlohmann::json::object()}};
}

nlohmann::json r2StandardProfile() {
    auto profile = minimalProfile("cloudflare-r2", "global", "standard");
    profile["meters"] = {
        {"egress", meter("egress", "egress_bytes", "byte", "gb", "0", "gb")},
        {"free_operations", meter("free_operations", "request", "operation", "", "0", "operation")},
        {"request_class_a", meter("request_class_a", "request", "operation", "million_operations", "4.50", "million_operations", "ceil_to_billing_unit", "1", "million_operations")},
        {"request_class_b", meter("request_class_b", "request", "operation", "million_operations", "0.36", "million_operations", "ceil_to_billing_unit", "10", "million_operations")},
        {"storage", meter("storage", "storage_byte_hours", "byte_hour", "gb_month", "0.015", "gb_month", "ceil_to_billing_unit", "10", "gb_month")}
    };
    profile["operation_map"] = {
        {"PutObject", opMap("request_class_a")},
        {"CopyObject", opMap("request_class_a")},
        {"ListObjectsV2", opMap("request_class_a")},
        {"GetObject", opMap("request_class_b")},
        {"HeadObject", opMap("request_class_b")},
        {"DeleteObject", opMap("free_operations")}
    };
    return profile;
}

nlohmann::json r2InfrequentAccessProfile() {
    auto profile = minimalProfile("cloudflare-r2", "global", "infrequent-access");
    profile["meters"] = {
        {"retrieval", meter("retrieval", "retrieval_bytes", "byte", "gb", "0.01", "gb")},
        {"storage", meter("storage", "storage_byte_hours", "byte_hour", "gb_month", "0.01", "gb_month", "ceil_to_billing_unit")},
    };
    profile["storage_rules"] = nlohmann::json::array({{{"minimum_storage_duration_days", 30}}});
    return profile;
}

nlohmann::json awsStandardProfile() {
    auto profile = minimalProfile("aws-s3", "us-east-1", "standard");
    profile["meters"] = {
        {"free_operations", meter("free_operations", "request", "operation", "", "0", "operation")},
        {"request_read", meter("request_read", "request", "operation", "request_1000", "0.0004", "request_1000")},
        {"request_write", meter("request_write", "request", "operation", "request_1000", "0.005", "request_1000")},
        {"storage", meter("storage", "storage_byte_hours", "byte_hour", "gb_month", "0.023", "gb_month")}
    };
    profile["operation_map"] = {
        {"PutObject", opMap("request_write")},
        {"ListObjectsV2", opMap("request_write")},
        {"GetObject", opMap("request_read")},
        {"HeadObject", opMap("request_read")},
        {"DeleteObject", opMap("free_operations")}
    };
    return profile;
}

nlohmann::json awsStandardIaProfile() {
    auto profile = minimalProfile("aws-s3", "us-east-1", "standard-ia");
    profile["meters"] = {
        {"retrieval", meter("retrieval", "retrieval_bytes", "byte", "gb", "0.01", "gb")},
        {"storage", meter("storage", "storage_byte_hours", "byte_hour", "gb_month", "0.0125", "gb_month")},
    };
    profile["storage_rules"] = nlohmann::json::array({{
        {"minimum_billable_object_size_bytes", 131072},
        {"minimum_storage_duration_days", 30}
    }});
    return profile;
}

nlohmann::json manifestForProfile(const nlohmann::json& profile, const std::string& href) {
    const auto payload = profile.dump();
    return {
        {"schema_version", "1.0"},
        {"kind", "vaulthalla.price_manifest"},
        {"catalog_version", profile.value("catalog_version", "fixture-catalog")},
        {"generated_at", "2026-05-26T20:00:10Z"},
        {"profile_count", 1},
        {"profiles", nlohmann::json::array({{
            {"profile_id", profile.value("profile_id", "")},
            {"provider", profile.at("provider").at("id")},
            {"region", profile.at("scope").at("region")},
            {"storage_class", profile.at("scope").at("storage_class")},
            {"currency", profile.at("scope").at("currency")},
            {"href", href},
            {"sha256", vh::storage::s3::curl::sha256Hex(payload)},
            {"signature", href + ".sig"},
            {"confidence_level", "high"},
            {"effective_at", "2026-05-26T20:00:10Z"}
        }})},
        {"full_catalog", nlohmann::json::object()},
        {"schemas", nlohmann::json::object()},
        {"integrity", {{"content_sha256", ""}, {"signature_alg", "Ed25519"}, {"signature_ref", "manifest.json.sig"}}}
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
    cfg.remote_refresh_enabled = true;
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

TEST(S3PricingTest, LocalEstimatorMatchesR2StandardReportingAndBudgetConservativeSemantics) {
    using namespace vh::storage::s3::pricing;
    const auto profile = RatingProfile::parse(r2StandardProfile());

    UsageInput usage;
    usage.provider_operation_counts["PutObject"] = "1000";
    usage.provider_operation_counts["GetObject"] = "1000";
    usage.storage_byte_hours["standard"] = "3650000000000"; // 5 GB-months.

    const auto reporting = LocalEstimator{}.estimate(profile, usage, {.mode = PriceEstimateMode::Reporting});
    EXPECT_EQ("0.00000000", reporting.estimated_cost);
    EXPECT_EQ("reporting", reporting.estimate_mode);
    EXPECT_EQ("apply_free_tiers", reporting.free_tier_policy);
    ASSERT_TRUE(reporting.free_tiers_applied.has_value());
    EXPECT_TRUE(*reporting.free_tiers_applied);
    EXPECT_EQ("5", reporting.free_tier_applied.at("storage"));

    const auto conservative = LocalEstimator{}.estimate(
        profile,
        usage,
        {.mode = PriceEstimateMode::BudgetConservative});
    EXPECT_EQ("4.93500000", conservative.estimated_cost);
    EXPECT_EQ("budget_conservative", conservative.estimate_mode);
    EXPECT_EQ("ignore_account_wide_free_tiers", conservative.free_tier_policy);
    ASSERT_TRUE(conservative.free_tiers_applied.has_value());
    EXPECT_FALSE(*conservative.free_tiers_applied);
    EXPECT_TRUE(conservative.free_tier_applied.empty());
    EXPECT_NE(
        std::find(
            conservative.unknowns.begin(),
            conservative.unknowns.end(),
            "account-level free tiers intentionally ignored for budget-safe estimate"),
        conservative.unknowns.end());
}

TEST(S3PricingTest, LocalEstimatorMatchesR2InfrequentAccessRetrievalAndEarlyDelete) {
    using namespace vh::storage::s3::pricing;
    const auto profile = RatingProfile::parse(r2InfrequentAccessProfile());

    UsageInput usage;
    usage.storage_byte_hours["infrequent-access"] = "7300000000000"; // 10 GB-months.
    usage.retrieval_bytes = "100000000000";
    usage.early_delete_gb_days["infrequent-access"] = "30";

    const auto result = LocalEstimator{}.estimate(profile, usage);
    EXPECT_EQ("1.11000000", result.estimated_cost);
    ASSERT_TRUE(result.breakdown.is_array());
    EXPECT_TRUE(std::ranges::any_of(result.breakdown, [](const nlohmann::json& item) {
        return item.value("meter_key", "") == "minimum_duration_penalty";
    }));
}

TEST(S3PricingTest, LocalEstimatorMatchesAwsStandardFixtureSemantics) {
    using namespace vh::storage::s3::pricing;
    const auto profile = RatingProfile::parse(awsStandardProfile());

    UsageInput usage;
    usage.storage_byte_hours["standard"] = "73000000000000"; // 100 GB-months.
    usage.provider_operation_counts["PutObject"] = "1000";
    usage.provider_operation_counts["GetObject"] = "1000";

    const auto result = LocalEstimator{}.estimate(profile, usage);
    EXPECT_EQ("2.30540000", result.estimated_cost);
}

TEST(S3PricingTest, LocalEstimatorAppliesMinimumObjectSizeRetrievalMinimumAndOperationAggregation) {
    using namespace vh::storage::s3::pricing;
    auto profileJson = awsStandardIaProfile();
    profileJson["meters"]["retrieval"]["rules"] = {{"minimum_retrieval_object_size_bytes", 131072}};
    profileJson["meters"]["request_write"] = meter(
        "request_write",
        "request",
        "operation",
        "request_1000",
        "0.01",
        "request_1000");
    profileJson["operation_map"] = {
        {"PutObject", opMap("request_write", "2")},
        {"CopyObject", opMap("request_write")}
    };
    const auto profile = RatingProfile::parse(profileJson);

    UsageInput usage;
    usage.storage_byte_hours["standard-ia"] = "1";
    usage.object_count_hours["standard-ia"] = "730";
    usage.retrieval_bytes = "1";
    usage.retrieval_object_count = "1";
    usage.provider_operation_counts["PutObject"] = "10";
    usage.provider_operation_counts["CopyObject"] = "5";
    usage.early_delete_gb_days["standard-ia"] = "1";

    const auto result = LocalEstimator{}.estimate(profile, usage);
    const auto storage = *std::ranges::find_if(result.breakdown, [](const nlohmann::json& item) {
        return item.value("meter_key", "") == "storage";
    });
    EXPECT_EQ("0.000131072", storage.at("billable_quantity"));
    const auto retrieval = *std::ranges::find_if(result.breakdown, [](const nlohmann::json& item) {
        return item.value("meter_key", "") == "retrieval";
    });
    EXPECT_EQ("0.000131072", retrieval.at("billable_quantity"));
    const auto write = *std::ranges::find_if(result.breakdown, [](const nlohmann::json& item) {
        return item.value("meter_key", "") == "request_write";
    });
    EXPECT_EQ("25", write.at("quantity"));
    EXPECT_TRUE(std::ranges::any_of(result.breakdown, [](const nlohmann::json& item) {
        return item.value("meter_key", "") == "minimum_duration_penalty";
    }));
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

TEST(S3PricingTest, CatalogStoreFetchesPrimaryArtifactsAndHydratesProfile) {
    const Ed25519TestKey key;
    const auto profile = r2StandardProfile();
    const auto profileBody = profile.dump();
    const auto manifestBody = manifestForProfile(
        profile,
        "profiles/cloudflare-r2/global/standard.json").dump();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = manifestBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(manifestBody), .error = ""});
    transport->replies.push({.status = 200, .body = profileBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(profileBody), .error = ""});

    const auto cacheRoot = std::filesystem::temp_directory_path() /
        ("vh_catalog_primary_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    vh::storage::s3::pricing::PriceCatalogStore store(strictPricingConfig(key.publicKeyPath()), transport, cacheRoot);
    const auto result = store.getProfile({"cloudflare-r2", "global", "standard"}, true);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(result.stale);
    EXPECT_EQ("api", result.source);
    EXPECT_EQ("cloudflare-r2/global/standard", result.profile.profile_id);

    ASSERT_EQ(4u, transport->requests.size());
    EXPECT_EQ("GET", transport->requests[0].method);
    EXPECT_EQ("http://price-bot.test/v1/artifacts/manifest.json", transport->requests[0].url);
    EXPECT_EQ("http://price-bot.test/v1/artifacts/profiles/cloudflare-r2/global/standard.json", transport->requests[2].url);
    std::filesystem::remove_all(cacheRoot);
}

TEST(S3PricingTest, CatalogStoreDoesNotCallUpstreamWhenRemoteRefreshDisabled) {
    auto cfg = testPricingConfig();
    cfg.remote_refresh_enabled = false;

    auto transport = std::make_shared<FakeTransport>();
    const auto cacheRoot = std::filesystem::temp_directory_path() /
        ("vh_catalog_no_refresh_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    vh::storage::s3::pricing::PriceCatalogStore store(cfg, transport, cacheRoot);
    const auto result = store.getProfile({"cloudflare-r2", "global", "standard"}, true);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(std::string::npos, result.error.find("remote refresh is disabled"));
    EXPECT_TRUE(transport->requests.empty());
    std::filesystem::remove_all(cacheRoot);
}

TEST(S3PricingTest, CatalogStoreFallsBackWhenPrimaryArtifactSourceFails) {
    const Ed25519TestKey key;
    auto cfg = strictPricingConfig(key.publicKeyPath());
    cfg.fallback_artifact_base_urls = {"http://mirror.test/v1"};
    const auto profile = r2StandardProfile();
    const auto profileBody = profile.dump();
    const auto manifestBody = manifestForProfile(
        profile,
        "profiles/cloudflare-r2/global/standard.json").dump();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 503, .body = "", .error = ""});
    transport->replies.push({.status = 200, .body = manifestBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(manifestBody), .error = ""});
    transport->replies.push({.status = 200, .body = profileBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(profileBody), .error = ""});

    const auto cacheRoot = std::filesystem::temp_directory_path() /
        ("vh_catalog_fallback_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    vh::storage::s3::pricing::PriceCatalogStore store(cfg, transport, cacheRoot);
    const auto result = store.getProfile({"cloudflare-r2", "global", "standard"}, true);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ("fallback", result.source);
    ASSERT_EQ(5u, transport->requests.size());
    EXPECT_EQ("http://mirror.test/v1/manifest.json", transport->requests[1].url);
    std::filesystem::remove_all(cacheRoot);
}

TEST(S3PricingTest, CatalogStoreUsesStaleVerifiedDiskCacheWhenRefreshFails) {
    const Ed25519TestKey key;
    const auto cacheRoot = std::filesystem::temp_directory_path() /
        ("vh_catalog_stale_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto profile = r2StandardProfile();
    const auto profileBody = profile.dump();
    const auto manifestBody = manifestForProfile(
        profile,
        "profiles/cloudflare-r2/global/standard.json").dump();

    {
        auto transport = std::make_shared<FakeTransport>();
        transport->replies.push({.status = 200, .body = manifestBody, .error = ""});
        transport->replies.push({.status = 200, .body = key.sign(manifestBody), .error = ""});
        transport->replies.push({.status = 200, .body = profileBody, .error = ""});
        transport->replies.push({.status = 200, .body = key.sign(profileBody), .error = ""});
        vh::storage::s3::pricing::PriceCatalogStore store(strictPricingConfig(key.publicKeyPath()), transport, cacheRoot);
        ASSERT_TRUE(store.getProfile({"cloudflare-r2", "global", "standard"}, true).ok);
    }

    auto failingTransport = std::make_shared<FakeTransport>();
    failingTransport->replies.push({.status = 503, .body = "", .error = ""});
    auto cfg = strictPricingConfig(key.publicKeyPath());
    cfg.refresh_interval_seconds = 60;
    vh::storage::s3::pricing::PriceCatalogStore failingStore(cfg, failingTransport, cacheRoot);
    const auto stale = failingStore.getProfile({"cloudflare-r2", "global", "standard"}, true);
    ASSERT_TRUE(stale.ok) << stale.error;
    EXPECT_TRUE(stale.stale);
    EXPECT_EQ("disk-cache", stale.source);
    EXPECT_EQ("cloudflare-r2/global/standard", stale.profile.profile_id);
    std::filesystem::remove_all(cacheRoot);
}

TEST(S3PricingTest, CatalogStoreRejectsCorruptProfileSignatureAndReportsMissingProfile) {
    const Ed25519TestKey key;
    const auto profile = r2StandardProfile();
    const auto profileBody = profile.dump();
    const auto manifestBody = manifestForProfile(
        profile,
        "profiles/cloudflare-r2/global/standard.json").dump();

    auto transport = std::make_shared<FakeTransport>();
    transport->replies.push({.status = 200, .body = manifestBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(manifestBody), .error = ""});
    transport->replies.push({.status = 200, .body = profileBody, .error = ""});
    transport->replies.push({.status = 200, .body = key.sign(profileBody + "tampered"), .error = ""});

    const auto cacheRoot = std::filesystem::temp_directory_path() /
        ("vh_catalog_corrupt_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    vh::storage::s3::pricing::PriceCatalogStore corruptStore(strictPricingConfig(key.publicKeyPath()), transport, cacheRoot);
    const auto corrupt = corruptStore.getProfile({"cloudflare-r2", "global", "standard"}, true);
    EXPECT_FALSE(corrupt.ok);
    EXPECT_NE(std::string::npos, corrupt.error.find("signature"));

    auto emptyManifest = manifestForProfile(profile, "profiles/cloudflare-r2/global/standard.json");
    emptyManifest["profiles"] = nlohmann::json::array();
    emptyManifest["profile_count"] = 0;
    const auto emptyManifestBody = emptyManifest.dump();
    auto missingTransport = std::make_shared<FakeTransport>();
    missingTransport->replies.push({.status = 200, .body = emptyManifestBody, .error = ""});
    missingTransport->replies.push({.status = 200, .body = key.sign(emptyManifestBody), .error = ""});
    const auto missingCacheRoot = cacheRoot.string() + "_missing";
    vh::storage::s3::pricing::PriceCatalogStore missingStore(
        strictPricingConfig(key.publicKeyPath()),
        missingTransport,
        missingCacheRoot);
    const auto missing = missingStore.getProfile({"cloudflare-r2", "global", "standard"}, true);
    EXPECT_FALSE(missing.ok);
    EXPECT_NE(std::string::npos, missing.error.find("not found"));
    std::filesystem::remove_all(cacheRoot);
    std::filesystem::remove_all(missingCacheRoot);
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
    report.catalog_source = "disk-cache";
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
    EXPECT_NE(std::string::npos, output.find("Catalog source: disk-cache"));
}

TEST(S3PricingTest, PriceBudgetValidationRejectsUnsafeInputs) {
    using namespace vh::storage::s3::pricing;

    EXPECT_TRUE(isSupportedPriceBudgetProvider("aws-s3"));
    EXPECT_TRUE(isSupportedPriceBudgetProvider("cloudflare-r2"));
    EXPECT_FALSE(isSupportedPriceBudgetProvider("generic-s3-compatible"));

    EXPECT_TRUE(isValidPriceBudgetDecimal("0"));
    EXPECT_TRUE(isValidPriceBudgetDecimal("12.34567890"));
    EXPECT_FALSE(isValidPriceBudgetDecimal("-1"));
    EXPECT_FALSE(isValidPriceBudgetDecimal("1.123456789"));
    EXPECT_FALSE(isValidPriceBudgetDecimal("1e6"));

    EXPECT_EQ("USD", normalizePriceBudgetCurrency("usd"));
    EXPECT_TRUE(isValidPriceBudgetCurrency("USD"));
    EXPECT_FALSE(isValidPriceBudgetCurrency("US"));
}

TEST(S3PricingTest, PriceBudgetDryRunFormatShowsFailingEnforcementDecision) {
    vh::storage::s3::pricing::PriceBudgetDecision decision;
    decision.allowed = false;
    decision.stalled = true;
    decision.reason = "S3 price budget exceeded for monthly global";
    vh::storage::s3::pricing::PriceBudgetPolicy policy;
    policy.id = 7;
    policy.scope = vh::storage::s3::pricing::PriceBudgetScope::Global;
    policy.mode = vh::storage::s3::pricing::PriceBudgetMode::Enforce;
    policy.currency = "USD";
    policy.max_monthly_cost = "10.00000000";
    decision.policies.push_back(policy);
    decision.checks.push_back({
        .policy_id = 7,
        .scope = vh::storage::s3::pricing::PriceBudgetScope::Global,
        .mode = vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        .window = vh::storage::s3::pricing::PriceBudgetWindow::Monthly,
        .currency = "USD",
        .limit = "10.00000000",
        .used_before = "9.50000000",
        .remaining_before = "0.50000000",
        .requested = "1.00000000",
        .exceeded = true
    });

    const auto output = vh::storage::s3::pricing::formatPriceBudgetDecisionForDryRun(decision);
    EXPECT_NE(std::string::npos, output.find("Applicable policies: 1"));
    EXPECT_NE(std::string::npos, output.find("Result: fail"));
    EXPECT_NE(std::string::npos, output.find("Enforcement would stall: yes"));
    EXPECT_NE(std::string::npos, output.find("remaining before 0.50000000"));
}
