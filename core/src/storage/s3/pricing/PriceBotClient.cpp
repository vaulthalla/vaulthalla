#include "storage/s3/pricing/PriceBotClient.hpp"

#include "log/Registry.hpp"
#include "storage/s3/curl/helpers.hpp"

#include <curl/curl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <paths.h>
#include <sodium.h>
#include <sstream>
#include <utility>

namespace vh::storage::s3::pricing {
namespace {

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string sanitizePathComponent(std::string value) {
    for (auto& ch : value) {
        const bool ok =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.';
        if (!ok) ch = '_';
    }
    return value.empty() ? std::string{"_"} : value;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

std::string trimSignature(std::string signature) {
    while (!signature.empty() && (
        signature.back() == '\n' ||
        signature.back() == '\r' ||
        signature.back() == ' ' ||
        signature.back() == '\t')) {
        signature.pop_back();
    }
    return signature;
}

bool verifyEd25519Pem(
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

void attachEstimateOptions(nlohmann::json& request, const PriceEstimateMode mode) {
    if (mode != PriceEstimateMode::BudgetConservative) return;
    request["options"] = {
        {"mode", toString(mode)},
        {"apply_free_tiers", false}
    };
}

} // namespace

HttpReply CurlHttpTransport::perform(const HttpRequest& request, const std::uint32_t timeoutMs) {
    vh::storage::s3::curl::ensureCurlGlobalInit();

    CURL* curl = curl_easy_init();
    if (!curl) return {.status = 0, .body = "", .error = "curl_easy_init failed"};

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, vh::storage::s3::curl::writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    curl_slist* headers = nullptr;
    for (const auto& header : request.headers)
        headers = curl_slist_append(headers, header.c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    std::string error;
    if (code != CURLE_OK) error = curl_easy_strerror(code);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return {.status = status, .body = std::move(body), .error = std::move(error)};
}

PriceBotClient::PriceBotClient(config::StorageRatesApiConfig config)
    : PriceBotClient(std::move(config), std::make_shared<CurlHttpTransport>()) {}

PriceBotClient::PriceBotClient(config::StorageRatesApiConfig config, std::shared_ptr<HttpTransport> transport)
    : config_(std::move(config)),
      transport_(std::move(transport)),
      cacheRoot_(paths::getBackingPath() / "price-cache") {}

PriceBotResponse<nlohmann::json> PriceBotClient::getHealth() {
    return getJsonWithCache("/health", cacheRoot_ / "health.json", true);
}

PriceBotResponse<nlohmann::json> PriceBotClient::getManifest(const bool forceRefresh) {
    return getArtifactJsonWithCache(
        "/v1/artifacts/manifest.json",
        "/v1/artifacts/manifest.json.sig",
        manifestCachePath(),
        forceRefresh,
        "manifest");
}

PriceBotResponse<RatingProfile> PriceBotClient::getProfile(
    const std::string& provider,
    const std::string& region,
    const std::string& storageClass,
    const bool forceRefresh) {
    const auto artifactPath = "/v1/artifacts/profiles/" + provider + "/" + region + "/" + storageClass + ".json";
    const auto result = getArtifactJsonWithCache(
        artifactPath,
        artifactPath + ".sig",
        profileCachePath(provider, region, storageClass),
        forceRefresh,
        "profile " + provider + "/" + region + "/" + storageClass);

    if (!result.ok) return PriceBotResponse<RatingProfile>::failure(result.error, result.http_status);

    try {
        return PriceBotResponse<RatingProfile>::success(RatingProfile::parse(result.value), result.stale, result.http_status);
    } catch (const std::exception& e) {
        return PriceBotResponse<RatingProfile>::failure(e.what(), result.http_status);
    }
}

PriceBotResponse<nlohmann::json> PriceBotClient::resolveProfile(
    const std::string& provider,
    const std::string& region,
    const std::vector<std::string>& storageClasses,
    const bool includeBundle) {
    const nlohmann::json request = {
        {"provider", provider},
        {"region", region},
        {"storage_classes", storageClasses},
        {"include_bundle", includeBundle}
    };
    return postJson("/v1/resolve-profile", request);
}

PriceBotResponse<EstimateResult> PriceBotClient::estimate(
    const std::string& provider,
    const std::string& region,
    const std::string& storageClass,
    const UsageInput& usage,
    const bool forceRefresh,
    const PriceEstimateMode mode) {
    nlohmann::json request = {
        {"provider", provider},
        {"region", region},
        {"storage_class", storageClass},
        {"usage", usage}
    };
    attachEstimateOptions(request, mode);
    return postEstimateWithCache(request, forceRefresh);
}

PriceBotResponse<EstimateResult> PriceBotClient::estimate(
    const nlohmann::json& profileJson,
    const UsageInput& usage,
    const bool forceRefresh,
    const PriceEstimateMode mode) {
    nlohmann::json request = {
        {"profile", profileJson},
        {"usage", usage}
    };
    attachEstimateOptions(request, mode);
    return postEstimateWithCache(request, forceRefresh);
}

std::string PriceBotClient::baseUrl() const {
    return trimTrailingSlash(config_.base_url.empty() ? std::string{kDefaultStorageRatesApiBaseUrl} : config_.base_url);
}

std::string PriceBotClient::url(const std::string& path) const {
    if (path.empty() || path.front() != '/') return baseUrl() + "/" + path;
    return baseUrl() + path;
}

std::filesystem::path PriceBotClient::manifestCachePath() const {
    return cacheRoot_ / "manifest.json";
}

std::filesystem::path PriceBotClient::profileCachePath(
    const std::string& provider,
    const std::string& region,
    const std::string& storageClass) const {
    return cacheRoot_ / "profiles" /
        sanitizePathComponent(provider) /
        sanitizePathComponent(region) /
        (sanitizePathComponent(storageClass) + ".json");
}

std::filesystem::path PriceBotClient::estimateCachePath(const nlohmann::json& request) const {
    const auto digest = vh::storage::s3::curl::sha256Hex(request.dump());
    return cacheRoot_ / "estimates" / (digest + ".json");
}

std::filesystem::path PriceBotClient::signatureCachePath(const std::filesystem::path& artifactCachePath) const {
    auto path = artifactCachePath;
    path += ".sig";
    return path;
}

PriceBotClient::CacheEntry PriceBotClient::readCache(const std::filesystem::path& path) const {
    CacheEntry entry;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return entry;
    entry.exists = true;
    entry.body = readFile(path);
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) return entry;
    const auto now = std::filesystem::file_time_type::clock::now();
    const auto age = now > modified ? now - modified : std::filesystem::file_time_type::duration::zero();
    entry.fresh = age <= std::chrono::seconds(config_.cache_ttl_seconds);
    return entry;
}

void PriceBotClient::writeCache(const std::filesystem::path& path, const std::string& body) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output << body;
}

PriceBotResponse<nlohmann::json> PriceBotClient::getJsonWithCache(
    const std::string& path,
    const std::filesystem::path& cachePath,
    const bool forceRefresh) {
    const auto cached = readCache(cachePath);
    if (!forceRefresh && cached.exists && cached.fresh) {
        try {
            return PriceBotResponse<nlohmann::json>::success(nlohmann::json::parse(cached.body), false, 200);
        } catch (...) {
            // Fall through to network refresh.
        }
    }

    const auto reply = transport_->perform({.method = "GET", .url = url(path), .body = "", .headers = {}}, config_.timeout_ms);
    if (reply.ok()) {
        try {
            auto parsed = nlohmann::json::parse(reply.body);
            writeCache(cachePath, reply.body);
            return PriceBotResponse<nlohmann::json>::success(std::move(parsed), false, reply.status);
        } catch (const std::exception& e) {
            if (!cached.exists) return PriceBotResponse<nlohmann::json>::failure(e.what(), reply.status);
        }
    }

    if (cached.exists) {
        try {
            return PriceBotResponse<nlohmann::json>::success(nlohmann::json::parse(cached.body), true, reply.status);
        } catch (const std::exception& e) {
            return PriceBotResponse<nlohmann::json>::failure(e.what(), reply.status);
        }
    }

    if (!reply.error.empty()) return PriceBotResponse<nlohmann::json>::failure(reply.error, reply.status);
    return PriceBotResponse<nlohmann::json>::failure(
        "price-bot returned HTTP " + std::to_string(reply.status),
        reply.status);
}

PriceBotResponse<nlohmann::json> PriceBotClient::getArtifactJsonWithCache(
    const std::string& artifactPath,
    const std::string& signaturePath,
    const std::filesystem::path& cachePath,
    const bool forceRefresh,
    const std::string& artifactLabel) {
    const auto signatureCache = signatureCachePath(cachePath);
    const auto cached = readCache(cachePath);
    const auto cachedSignature = readCache(signatureCache);

    const auto parseVerified = [&](const std::string& payload,
                                   const std::optional<std::string>& signature,
                                   const bool stale,
                                   const long httpStatus) -> PriceBotResponse<nlohmann::json> {
        if (!verifyArtifactIfConfigured(artifactLabel, payload, signature))
            return PriceBotResponse<nlohmann::json>::failure("signature verification failed", httpStatus);
        try {
            return PriceBotResponse<nlohmann::json>::success(nlohmann::json::parse(payload), stale, httpStatus);
        } catch (const std::exception& e) {
            return PriceBotResponse<nlohmann::json>::failure(e.what(), httpStatus);
        }
    };

    if (!forceRefresh && cached.exists && cached.fresh) {
        const auto signature = cachedSignature.exists
            ? std::make_optional(cachedSignature.body)
            : std::optional<std::string>{};
        auto cachedResult = parseVerified(cached.body, signature, false, 200);
        if (cachedResult.ok) return cachedResult;
        // Malformed or unverifiable cached artifacts can be recovered by a network refresh below.
    }

    const auto reply = transport_->perform(
        {.method = "GET", .url = url(artifactPath), .body = "", .headers = {}},
        config_.timeout_ms);
    if (reply.ok()) {
        const auto signature = fetchSignature(signaturePath);
        auto result = parseVerified(reply.body, signature, false, reply.status);
        if (!result.ok) {
            if (result.error == "signature verification failed" || !cached.exists)
                return result;

            const auto cachedSignatureValue = cachedSignature.exists
                ? std::make_optional(cachedSignature.body)
                : std::optional<std::string>{};
            return parseVerified(cached.body, cachedSignatureValue, true, reply.status);
        }

        writeCache(cachePath, reply.body);
        if (signature) {
            writeCache(signatureCache, *signature);
        } else {
            std::error_code ec;
            std::filesystem::remove(signatureCache, ec);
        }
        return result;
    }

    if (cached.exists) {
        const auto signature = cachedSignature.exists
            ? std::make_optional(cachedSignature.body)
            : std::optional<std::string>{};
        return parseVerified(cached.body, signature, true, reply.status);
    }

    if (!reply.error.empty()) return PriceBotResponse<nlohmann::json>::failure(reply.error, reply.status);
    return PriceBotResponse<nlohmann::json>::failure(
        "price-bot returned HTTP " + std::to_string(reply.status),
        reply.status);
}

PriceBotResponse<nlohmann::json> PriceBotClient::postJson(const std::string& path, const nlohmann::json& body) {
    const auto payload = body.dump();
    const auto reply = transport_->perform(
        {
            .method = "POST",
            .url = url(path),
            .body = payload,
            .headers = {"Content-Type: application/json"}
        },
        config_.timeout_ms);
    if (!reply.ok()) {
        if (!reply.error.empty()) return PriceBotResponse<nlohmann::json>::failure(reply.error, reply.status);
        return PriceBotResponse<nlohmann::json>::failure(
            "price-bot returned HTTP " + std::to_string(reply.status),
            reply.status);
    }

    try {
        return PriceBotResponse<nlohmann::json>::success(nlohmann::json::parse(reply.body), false, reply.status);
    } catch (const std::exception& e) {
        return PriceBotResponse<nlohmann::json>::failure(e.what(), reply.status);
    }
}

PriceBotResponse<EstimateResult> PriceBotClient::postEstimateWithCache(
    const nlohmann::json& request,
    const bool forceRefresh) {
    const auto cachePath = estimateCachePath(request);
    const auto cached = readCache(cachePath);
    if (!forceRefresh && cached.exists && cached.fresh) {
        try {
            return PriceBotResponse<EstimateResult>::success(
                EstimateResult::parse(nlohmann::json::parse(cached.body)),
                false,
                200);
        } catch (...) {
            // Fall through to network refresh.
        }
    }

    const auto result = postJson("/v1/estimate", request);
    if (result.ok) {
        try {
            writeCache(cachePath, result.value.dump());
            return PriceBotResponse<EstimateResult>::success(
                EstimateResult::parse(result.value),
                false,
                result.http_status);
        } catch (const std::exception& e) {
            if (!cached.exists) return PriceBotResponse<EstimateResult>::failure(e.what(), result.http_status);
        }
    }

    if (cached.exists) {
        try {
            return PriceBotResponse<EstimateResult>::success(
                EstimateResult::parse(nlohmann::json::parse(cached.body)),
                true,
                result.http_status);
        } catch (const std::exception& e) {
            return PriceBotResponse<EstimateResult>::failure(e.what(), result.http_status);
        }
    }

    return PriceBotResponse<EstimateResult>::failure(result.error, result.http_status);
}

bool PriceBotClient::verifyArtifactIfConfigured(
    const std::string& artifactLabel,
    const std::string& payload,
    const std::optional<std::string>& signatureBase64) {
    if (!config_.signature_public_key_path) {
        if (config_.signature_warning_only) {
            log::Registry::storage()->warn(
                "[PriceBot] Signature verification is warning-only for {}: no Ed25519 public key path is configured",
                artifactLabel);
        } else {
            log::Registry::storage()->error(
                "[PriceBot] Signature verification is strict for {} but no Ed25519 public key path is configured",
                artifactLabel);
        }
        return config_.signature_warning_only;
    }

    const auto signature = signatureBase64 ? trimSignature(*signatureBase64) : std::string{};
    if (signature.empty()) {
        if (config_.signature_warning_only) {
            log::Registry::storage()->warn(
                "[PriceBot] Signature verification is warning-only for {}: signature artifact is unavailable",
                artifactLabel);
        } else {
            log::Registry::storage()->error(
                "[PriceBot] Signature verification failed for {}: signature artifact is unavailable",
                artifactLabel);
        }
        return config_.signature_warning_only;
    }

    std::string error;
    if (verifyEd25519Pem(*config_.signature_public_key_path, payload, signature, error))
        return true;

    if (config_.signature_warning_only) {
        log::Registry::storage()->warn(
            "[PriceBot] Signature verification warning for {}: {}",
            artifactLabel,
            error);
        return true;
    }

    log::Registry::storage()->error(
        "[PriceBot] Signature verification failed for {}: {}",
        artifactLabel,
        error);
    return false;
}

std::optional<std::string> PriceBotClient::fetchSignature(const std::string& signaturePath) {
    const auto reply = transport_->perform(
        {.method = "GET", .url = url(signaturePath), .body = "", .headers = {}},
        config_.timeout_ms);
    if (!reply.ok()) return std::nullopt;

    return trimSignature(reply.body).empty() ? std::nullopt : std::make_optional(reply.body);
}

} // namespace vh::storage::s3::pricing
