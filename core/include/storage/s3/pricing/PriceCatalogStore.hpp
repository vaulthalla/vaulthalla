#pragma once

#include "config/Config.hpp"
#include "storage/s3/pricing/PriceBotClient.hpp"
#include "storage/s3/pricing/PriceCatalog.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vh::storage::s3::pricing {

struct PriceCatalogProfileResult {
    bool ok{false};
    bool stale{false};
    std::string source;
    std::string error;
    RatingProfile profile;
};

class IPriceCatalogStore {
public:
    virtual ~IPriceCatalogStore() = default;

    [[nodiscard]] virtual PriceCatalogProfileResult getProfile(
        const PriceProfileTarget& target,
        bool forceRefresh = false) = 0;
};

class PriceCatalogStore final : public IPriceCatalogStore {
public:
    explicit PriceCatalogStore(config::StorageRatesApiConfig config);
    PriceCatalogStore(
        config::StorageRatesApiConfig config,
        std::shared_ptr<HttpTransport> transport);
    PriceCatalogStore(
        config::StorageRatesApiConfig config,
        std::shared_ptr<HttpTransport> transport,
        std::filesystem::path cacheRoot);

    [[nodiscard]] PriceCatalogProfileResult getProfile(
        const PriceProfileTarget& target,
        bool forceRefresh = false) override;

    [[nodiscard]] PriceCatalogRefreshResult refresh();
    [[nodiscard]] PriceCatalogRefreshResult hydrateFromDisk();

private:
    config::StorageRatesApiConfig config_;
    std::shared_ptr<HttpTransport> transport_;
    std::filesystem::path cacheRoot_;
    PriceCatalog catalog_;
    bool hydrated_{false};

    struct ArtifactSource {
        std::string base_url;
        std::string label;
    };

    struct Artifact {
        std::string payload;
        std::optional<std::string> signature;
        long http_status{0};
    };

    [[nodiscard]] bool refreshDue() const;
    [[nodiscard]] std::vector<ArtifactSource> artifactSources() const;
    [[nodiscard]] PriceCatalogRefreshResult refreshFromSource(const ArtifactSource& source);
    [[nodiscard]] std::optional<Artifact> fetchArtifact(const ArtifactSource& source, const std::string& path) const;
    [[nodiscard]] PriceCatalogRefreshResult parseCatalogFromArtifacts(
        const std::string& manifestPayload,
        const std::optional<std::string>& manifestSignature,
        const std::string& sourceLabel,
        const std::function<std::optional<Artifact>(const std::string&)>& loader) const;
    [[nodiscard]] bool verifyArtifact(
        const std::string& label,
        const std::string& payload,
        const std::optional<std::string>& signatureBase64) const;
    [[nodiscard]] std::filesystem::path cachePath(const std::string& artifactPath) const;
    void writeArtifactCache(const std::string& artifactPath, const std::string& payload) const;
};

} // namespace vh::storage::s3::pricing
