#pragma once

#include "config/Config.hpp"
#include "storage/s3/pricing/PriceBotModels.hpp"

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace vh::storage::s3::pricing {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::string body;
    std::vector<std::string> headers;
};

struct HttpReply {
    long status{0};
    std::string body;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 300 && error.empty(); }
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    [[nodiscard]] virtual HttpReply perform(const HttpRequest& request, std::uint32_t timeoutMs) = 0;
};

class CurlHttpTransport final : public HttpTransport {
public:
    [[nodiscard]] HttpReply perform(const HttpRequest& request, std::uint32_t timeoutMs) override;
};

template <typename T>
struct PriceBotResponse {
    bool ok{false};
    bool stale{false};
    long http_status{0};
    std::string error;
    T value{};

    [[nodiscard]] static PriceBotResponse success(T value, const bool stale = false, const long httpStatus = 200) {
        PriceBotResponse out;
        out.ok = true;
        out.stale = stale;
        out.http_status = httpStatus;
        out.value = std::move(value);
        return out;
    }

    [[nodiscard]] static PriceBotResponse failure(std::string error, const long httpStatus = 0) {
        PriceBotResponse out;
        out.ok = false;
        out.http_status = httpStatus;
        out.error = std::move(error);
        return out;
    }
};

class IPriceBotClient {
public:
    virtual ~IPriceBotClient() = default;

    [[nodiscard]] virtual PriceBotResponse<nlohmann::json> getHealth() = 0;
    [[nodiscard]] virtual PriceBotResponse<nlohmann::json> getManifest(bool forceRefresh = false) = 0;
    [[nodiscard]] virtual PriceBotResponse<RatingProfile> getProfile(
        const std::string& provider,
        const std::string& region,
        const std::string& storageClass,
        bool forceRefresh = false) = 0;
    [[nodiscard]] virtual PriceBotResponse<nlohmann::json> resolveProfile(
        const std::string& provider,
        const std::string& region,
        const std::vector<std::string>& storageClasses,
        bool includeBundle) = 0;
    [[nodiscard]] virtual PriceBotResponse<EstimateResult> estimate(
        const std::string& provider,
        const std::string& region,
        const std::string& storageClass,
        const UsageInput& usage,
        bool forceRefresh = false,
        PriceEstimateMode mode = PriceEstimateMode::Reporting) = 0;
    [[nodiscard]] virtual PriceBotResponse<EstimateResult> estimate(
        const nlohmann::json& profileJson,
        const UsageInput& usage,
        bool forceRefresh = false,
        PriceEstimateMode mode = PriceEstimateMode::Reporting) = 0;
};

class PriceBotClient final : public IPriceBotClient {
public:
    explicit PriceBotClient(config::StorageRatesApiConfig config);
    PriceBotClient(config::StorageRatesApiConfig config, std::shared_ptr<HttpTransport> transport);

    [[nodiscard]] PriceBotResponse<nlohmann::json> getHealth() override;
    [[nodiscard]] PriceBotResponse<nlohmann::json> getManifest(bool forceRefresh = false) override;
    [[nodiscard]] PriceBotResponse<RatingProfile> getProfile(
        const std::string& provider,
        const std::string& region,
        const std::string& storageClass,
        bool forceRefresh = false) override;
    [[nodiscard]] PriceBotResponse<nlohmann::json> resolveProfile(
        const std::string& provider,
        const std::string& region,
        const std::vector<std::string>& storageClasses,
        bool includeBundle) override;
    [[nodiscard]] PriceBotResponse<EstimateResult> estimate(
        const std::string& provider,
        const std::string& region,
        const std::string& storageClass,
        const UsageInput& usage,
        bool forceRefresh = false,
        PriceEstimateMode mode = PriceEstimateMode::Reporting) override;
    [[nodiscard]] PriceBotResponse<EstimateResult> estimate(
        const nlohmann::json& profileJson,
        const UsageInput& usage,
        bool forceRefresh = false,
        PriceEstimateMode mode = PriceEstimateMode::Reporting) override;

private:
    struct CacheEntry {
        bool exists{false};
        bool fresh{false};
        std::string body;
    };

    config::StorageRatesApiConfig config_;
    std::shared_ptr<HttpTransport> transport_;
    std::filesystem::path cacheRoot_;

    [[nodiscard]] std::string baseUrl() const;
    [[nodiscard]] std::string url(const std::string& path) const;
    [[nodiscard]] std::filesystem::path manifestCachePath() const;
    [[nodiscard]] std::filesystem::path profileCachePath(
        const std::string& provider,
        const std::string& region,
        const std::string& storageClass) const;
    [[nodiscard]] std::filesystem::path estimateCachePath(const nlohmann::json& request) const;
    [[nodiscard]] std::filesystem::path signatureCachePath(const std::filesystem::path& artifactCachePath) const;
    [[nodiscard]] CacheEntry readCache(const std::filesystem::path& path) const;
    void writeCache(const std::filesystem::path& path, const std::string& body) const;
    [[nodiscard]] PriceBotResponse<nlohmann::json> getJsonWithCache(
        const std::string& path,
        const std::filesystem::path& cachePath,
        bool forceRefresh);
    [[nodiscard]] PriceBotResponse<nlohmann::json> getArtifactJsonWithCache(
        const std::string& artifactPath,
        const std::string& signaturePath,
        const std::filesystem::path& cachePath,
        bool forceRefresh,
        const std::string& artifactLabel);
    [[nodiscard]] PriceBotResponse<nlohmann::json> postJson(
        const std::string& path,
        const nlohmann::json& body);
    [[nodiscard]] PriceBotResponse<EstimateResult> postEstimateWithCache(
        const nlohmann::json& request,
        bool forceRefresh);
    [[nodiscard]] bool verifyArtifactIfConfigured(
        const std::string& artifactLabel,
        const std::string& payload,
        const std::optional<std::string>& signatureBase64);
    [[nodiscard]] std::optional<std::string> fetchSignature(const std::string& signaturePath);
};

} // namespace vh::storage::s3::pricing
