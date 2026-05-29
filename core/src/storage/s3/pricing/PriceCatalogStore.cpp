#include "storage/s3/pricing/PriceCatalogStore.hpp"

#include "log/Registry.hpp"
#include "storage/s3/curl/helpers.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <paths.h>
#include <sodium.h>
#include <sstream>
#include <utility>
#include <vector>

namespace vh::storage::s3::pricing {
namespace {

std::string catalogTrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string catalogTrimSignature(std::string signature) {
    while (!signature.empty() && (
        signature.back() == '\n' ||
        signature.back() == '\r' ||
        signature.back() == ' ' ||
        signature.back() == '\t')) {
        signature.pop_back();
    }
    return signature;
}

std::string catalogReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

bool catalogWriteFile(const std::filesystem::path& path, const std::string& payload) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << payload;
    return static_cast<bool>(output);
}

std::filesystem::path catalogSafeRelativePath(const std::string& artifactPath) {
    std::filesystem::path out;
    std::string segment;
    const auto flush = [&] {
        if (segment.empty() || segment == "." || segment == "..") {
            segment.clear();
            return;
        }
        for (auto& ch : segment) {
            const bool ok =
                (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' || ch == '.';
            if (!ok) ch = '_';
        }
        out /= segment;
        segment.clear();
    };

    for (const auto ch : artifactPath) {
        if (ch == '/' || ch == '\\') flush();
        else segment.push_back(ch);
    }
    flush();
    return out;
}

bool catalogArtifactPathAllowed(const std::string& artifactPath) {
    if (artifactPath.empty() || artifactPath.front() == '/' || artifactPath.contains('\\'))
        return false;
    std::string segment;
    const auto segmentOk = [](const std::string& value) {
        if (value.empty() || value == "." || value == "..") return false;
        return std::ranges::all_of(value, [](const char ch) {
            return (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' || ch == '.';
        });
    };
    for (const auto ch : artifactPath) {
        if (ch == '/') {
            if (!segmentOk(segment)) return false;
            segment.clear();
        } else {
            segment.push_back(ch);
        }
    }
    return segmentOk(segment);
}

std::string catalogJoinUrl(const std::string& baseUrl, const std::string& path) {
    auto base = catalogTrimTrailingSlash(baseUrl);
    if (path.empty()) return base;
    if (path.front() == '/') return base + path;
    return base + "/" + path;
}

std::string catalogPrimaryArtifactBaseUrl(const config::StorageRatesApiConfig& config) {
    auto base = catalogTrimTrailingSlash(
        config.base_url.empty() ? std::string{kDefaultStorageRatesApiBaseUrl} : config.base_url);
    if (base.ends_with("/v1/artifacts")) return base;
    return base + "/v1/artifacts";
}

bool catalogVerifyEd25519Pem(
    const std::filesystem::path& publicKeyPath,
    const std::string& payload,
    const std::string& signatureBase64,
    std::string& error) {
    if (sodium_init() < 0) {
        error = "libsodium initialization failed";
        return false;
    }

    std::vector<unsigned char> signature(crypto_sign_BYTES);
    size_t signatureLen = 0;
    if (sodium_base642bin(
            signature.data(),
            signature.size(),
            signatureBase64.c_str(),
            signatureBase64.size(),
            nullptr,
            &signatureLen,
            nullptr,
            sodium_base64_VARIANT_ORIGINAL) != 0 ||
        signatureLen != crypto_sign_BYTES) {
        error = "invalid Ed25519 signature encoding";
        return false;
    }

    BIO* bio = BIO_new_file(publicKeyPath.string().c_str(), "r");
    if (!bio) {
        error = "unable to read Ed25519 public key";
        return false;
    }
    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!key) {
        error = "unable to parse Ed25519 public key";
        return false;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(key);
        error = "unable to allocate OpenSSL verification context";
        return false;
    }

    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) != 1) {
        error = "unable to initialize Ed25519 verifier";
    } else {
        const auto rc = EVP_DigestVerify(
            ctx,
            signature.data(),
            signatureLen,
            reinterpret_cast<const unsigned char*>(payload.data()),
            payload.size());
        if (rc == 1) ok = true;
        else error = "Ed25519 signature verification failed";
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

bool catalogFileFresh(const std::filesystem::path& path, const std::chrono::seconds maxAge) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    const auto now = std::filesystem::file_time_type::clock::now();
    const auto age = now > modified ? now - modified : std::filesystem::file_time_type::duration::zero();
    return age <= maxAge;
}

PriceCatalogRefreshResult catalogFailure(std::string source, std::string error) {
    PriceCatalogRefreshResult result;
    result.ok = false;
    result.stale = false;
    result.source = std::move(source);
    result.error = std::move(error);
    return result;
}

} // namespace

PriceCatalogStore::PriceCatalogStore(config::StorageRatesApiConfig config)
    : PriceCatalogStore(std::move(config), std::make_shared<CurlHttpTransport>()) {}

PriceCatalogStore::PriceCatalogStore(
    config::StorageRatesApiConfig config,
    std::shared_ptr<HttpTransport> transport)
    : PriceCatalogStore(
        std::move(config),
        std::move(transport),
        paths::getBackingPath() / "price-cache" / "artifacts") {}

PriceCatalogStore::PriceCatalogStore(
    config::StorageRatesApiConfig config,
    std::shared_ptr<HttpTransport> transport,
    std::filesystem::path cacheRoot)
    : config_(std::move(config)),
      transport_(std::move(transport)),
      cacheRoot_(std::move(cacheRoot)) {}

PriceCatalogProfileResult PriceCatalogStore::getProfile(
    const PriceProfileTarget& target,
    const bool forceRefresh) {
    if (!hydrated_) {
        const auto disk = hydrateFromDisk();
        if (disk.ok) catalog_ = disk.catalog;
        hydrated_ = true;
    }

    if (forceRefresh || catalog_.empty() || refreshDue()) {
        const auto refreshed = refresh();
        if (refreshed.ok) {
            catalog_ = refreshed.catalog;
        } else if (!catalog_.empty()) {
            catalog_.stale = true;
            catalog_.source = kCatalogSourceDiskCache;
            log::Registry::storage()->warn(
                "[PriceCatalog] Refresh failed; using stale verified cache: {}",
                refreshed.error);
        } else {
            return {
                .ok = false,
                .stale = false,
                .source = {},
                .error = refreshed.error.empty() ? "pricing catalog unavailable" : refreshed.error,
                .profile = {}
            };
        }
    }

    const auto profile = catalog_.lookup(target);
    if (!profile) {
        return {
            .ok = false,
            .stale = catalog_.stale,
            .source = catalog_.source,
            .error = "pricing profile not found in catalog: " + target.profileId(),
            .profile = {}
        };
    }

    return {
        .ok = true,
        .stale = catalog_.stale,
        .source = catalog_.source,
        .error = {},
        .profile = *profile
    };
}

PriceCatalogRefreshResult PriceCatalogStore::refresh() {
    // TODO: Prefer catalog/full.json.zst when Vaulthalla grows a small zstd
    // dependency. For now, manifest + signed profiles keeps local estimation
    // independent from /v1/estimate without widening core compression deps.
    std::string lastError;
    for (const auto& source : artifactSources()) {
        auto result = refreshFromSource(source);
        if (result.ok) {
            result.catalog.source = source.label;
            result.source = source.label;
            return result;
        }
        lastError = result.error;
        log::Registry::storage()->warn(
            "[PriceCatalog] Artifact source {} failed: {}",
            source.label,
            result.error);
    }
    return catalogFailure({}, lastError.empty() ? "all artifact sources failed" : lastError);
}

PriceCatalogRefreshResult PriceCatalogStore::hydrateFromDisk() {
    const auto manifestPath = cachePath("manifest.json");
    const auto signaturePath = cachePath("manifest.json.sig");
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath, ec))
        return catalogFailure(kCatalogSourceDiskCache, "no cached manifest");

    const auto manifestPayload = catalogReadFile(manifestPath);
    const auto manifestSignature = std::filesystem::exists(signaturePath, ec)
        ? std::make_optional(catalogReadFile(signaturePath))
        : std::optional<std::string>{};

    auto loader = [&](const std::string& path) -> std::optional<Artifact> {
        const auto payloadPath = cachePath(path);
        if (!std::filesystem::exists(payloadPath, ec)) return std::nullopt;
        const auto sigPath = cachePath(path + ".sig");
        return Artifact{
            .payload = catalogReadFile(payloadPath),
            .signature = std::filesystem::exists(sigPath, ec)
                ? std::make_optional(catalogReadFile(sigPath))
                : std::optional<std::string>{},
            .http_status = 200
        };
    };

    auto result = parseCatalogFromArtifacts(
        manifestPayload,
        manifestSignature,
        kCatalogSourceDiskCache,
        loader);
    if (!result.ok) return result;

    result.catalog.source = kCatalogSourceDiskCache;
    result.catalog.stale = !catalogFileFresh(manifestPath, std::chrono::seconds(config_.cache_ttl_seconds));
    result.stale = result.catalog.stale;
    result.source = kCatalogSourceDiskCache;
    return result;
}

bool PriceCatalogStore::refreshDue() const {
    return !catalogFileFresh(cachePath("manifest.json"), std::chrono::seconds(config_.refresh_interval_seconds));
}

std::vector<PriceCatalogStore::ArtifactSource> PriceCatalogStore::artifactSources() const {
    std::vector<ArtifactSource> sources;
    sources.push_back({.base_url = catalogPrimaryArtifactBaseUrl(config_), .label = kCatalogSourceApi});
    for (const auto& fallback : config_.fallback_artifact_base_urls) {
        if (fallback.empty()) continue;
        sources.push_back({.base_url = catalogTrimTrailingSlash(fallback), .label = kCatalogSourceFallback});
    }
    return sources;
}

PriceCatalogRefreshResult PriceCatalogStore::refreshFromSource(const ArtifactSource& source) {
    const auto manifest = fetchArtifact(source, "manifest.json");
    if (!manifest)
        return catalogFailure(source.label, "manifest fetch failed");

    std::map<std::string, Artifact> fetchedProfiles;
    auto loader = [&](const std::string& path) -> std::optional<Artifact> {
        auto artifact = fetchArtifact(source, path);
        if (artifact) fetchedProfiles[path] = *artifact;
        return artifact;
    };

    auto result = parseCatalogFromArtifacts(
        manifest->payload,
        manifest->signature,
        source.label,
        loader);
    if (!result.ok) return result;

    writeArtifactCache("manifest.json", manifest->payload);
    if (manifest->signature) writeArtifactCache("manifest.json.sig", *manifest->signature);
    for (const auto& [href, artifact] : fetchedProfiles) {
        writeArtifactCache(href, artifact.payload);
        if (artifact.signature) writeArtifactCache(href + ".sig", *artifact.signature);
    }

    result.catalog.source = source.label;
    result.source = source.label;
    return result;
}

std::optional<PriceCatalogStore::Artifact> PriceCatalogStore::fetchArtifact(
    const ArtifactSource& source,
    const std::string& path) const {
    const auto reply = transport_->perform(
        {.method = "GET", .url = catalogJoinUrl(source.base_url, path), .body = "", .headers = {}},
        config_.timeout_ms);
    if (!reply.ok()) return std::nullopt;

    const auto signatureReply = transport_->perform(
        {.method = "GET", .url = catalogJoinUrl(source.base_url, path + ".sig"), .body = "", .headers = {}},
        config_.timeout_ms);
    return Artifact{
        .payload = reply.body,
        .signature = signatureReply.ok()
            ? std::make_optional(signatureReply.body)
            : std::optional<std::string>{},
        .http_status = reply.status
    };
}

PriceCatalogRefreshResult PriceCatalogStore::parseCatalogFromArtifacts(
    const std::string& manifestPayload,
    const std::optional<std::string>& manifestSignature,
    const std::string& sourceLabel,
    const std::function<std::optional<Artifact>(const std::string&)>& loader) const {
    if (!verifyArtifact("manifest", manifestPayload, manifestSignature))
        return catalogFailure(sourceLabel, "manifest signature verification failed");

    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(manifestPayload);
    } catch (const std::exception& e) {
        return catalogFailure(sourceLabel, e.what());
    }

    if (manifest.value("kind", "") != "vaulthalla.price_manifest")
        return catalogFailure(sourceLabel, "invalid price manifest kind");

    PriceCatalog catalog;
    catalog.catalog_version = manifest.value("catalog_version", "");
    catalog.source = sourceLabel;

    for (const auto& ref : manifest.value("profiles", nlohmann::json::array())) {
        const auto href = ref.value("href", "");
        if (href.empty())
            return catalogFailure(sourceLabel, "manifest profile entry missing href");
        if (!catalogArtifactPathAllowed(href))
            return catalogFailure(sourceLabel, "manifest profile entry has unsafe href: " + href);

        const auto artifact = loader(href);
        if (!artifact)
            return catalogFailure(sourceLabel, "profile fetch failed: " + href);

        if (!verifyArtifact("profile " + href, artifact->payload, artifact->signature))
            return catalogFailure(sourceLabel, "profile signature verification failed: " + href);

        const auto expectedSha = ref.value("sha256", "");
        if (!expectedSha.empty() && vh::storage::s3::curl::sha256Hex(artifact->payload) != expectedSha)
            return catalogFailure(sourceLabel, "profile hash mismatch: " + href);

        try {
            catalog.put(RatingProfile::parse(nlohmann::json::parse(artifact->payload)));
        } catch (const std::exception& e) {
            return catalogFailure(sourceLabel, e.what());
        }
    }

    return {.ok = true, .stale = false, .source = sourceLabel, .error = {}, .catalog = std::move(catalog)};
}

bool PriceCatalogStore::verifyArtifact(
    const std::string& label,
    const std::string& payload,
    const std::optional<std::string>& signatureBase64) const {
    if (!config_.signature_public_key_path) {
        if (config_.signature_warning_only) {
            log::Registry::storage()->warn(
                "[PriceCatalog] Signature verification is warning-only for {}: no Ed25519 public key path is configured",
                label);
        } else {
            log::Registry::storage()->error(
                "[PriceCatalog] Signature verification is strict for {} but no Ed25519 public key path is configured",
                label);
        }
        return config_.signature_warning_only;
    }

    const auto signature = signatureBase64 ? catalogTrimSignature(*signatureBase64) : std::string{};
    if (signature.empty()) {
        if (config_.signature_warning_only) {
            log::Registry::storage()->warn(
                "[PriceCatalog] Signature verification is warning-only for {}: signature artifact is unavailable",
                label);
        } else {
            log::Registry::storage()->error(
                "[PriceCatalog] Signature verification failed for {}: signature artifact is unavailable",
                label);
        }
        return config_.signature_warning_only;
    }

    std::string error;
    if (catalogVerifyEd25519Pem(*config_.signature_public_key_path, payload, signature, error))
        return true;

    if (config_.signature_warning_only) {
        log::Registry::storage()->warn(
            "[PriceCatalog] Signature verification warning for {}: {}",
            label,
            error);
        return true;
    }

    log::Registry::storage()->error(
        "[PriceCatalog] Signature verification failed for {}: {}",
        label,
        error);
    return false;
}

std::filesystem::path PriceCatalogStore::cachePath(const std::string& artifactPath) const {
    return cacheRoot_ / catalogSafeRelativePath(artifactPath);
}

void PriceCatalogStore::writeArtifactCache(const std::string& artifactPath, const std::string& payload) const {
    catalogWriteFile(cachePath(artifactPath), payload);
}

} // namespace vh::storage::s3::pricing
