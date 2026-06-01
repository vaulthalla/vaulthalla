#include "protocols/s3/Router.hpp"

#include "config/Registry.hpp"
#include "identities/User.hpp"
#include "log/Registry.hpp"
#include "protocols/s3/Xml.hpp"
#include "storage/s3/pricing/GatewayPriceEstimate.hpp"
#include "storage/s3/pricing/PriceBudget.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <pugixml.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

namespace vh::protocols::s3 {

namespace {
using Action = rbac::permission::vault::FilesystemAction;

std::string requestId() {
    return boost::uuids::to_string(boost::uuids::random_generator()());
}

std::string targetPath(const std::string& target) {
    const auto pos = target.find('?');
    return pos == std::string::npos ? target : target.substr(0, pos);
}

std::string targetQuery(const std::string& target) {
    const auto pos = target.find('?');
    return pos == std::string::npos ? "" : target.substr(pos + 1);
}

std::string pctDecode(const std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = std::string(value.substr(i + 1, 2));
            char* end = nullptr;
            const auto v = std::strtoul(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> out;
    std::stringstream ss(query);
    std::string part;
    while (std::getline(ss, part, '&')) {
        if (part.empty()) continue;
        const auto eq = part.find('=');
        out[pctDecode(part.substr(0, eq))] = eq == std::string::npos ? "" : pctDecode(part.substr(eq + 1));
    }
    return out;
}

bool hasQuery(const std::map<std::string, std::string>& query, const std::string& key) {
    return query.find(key) != query.end();
}

bool looksLikeIpAddressOrLocalhost(const std::string& host) {
    if (host == "localhost") return true;
    return std::ranges::all_of(host, [](const unsigned char c) {
        return std::isdigit(c) || c == '.' || c == ':';
    });
}

std::optional<std::string> virtualHostedBucket(const Router::Request& request) {
    const auto& cfg = config::Registry::get().s3_gateway;
    if (!cfg.allow_virtual_hosted_style) return std::nullopt;

    const auto it = request.find(http::field::host);
    if (it == request.end()) return std::nullopt;

    auto host = std::string(it->value());
    const auto colon = host.find(':');
    if (colon != std::string::npos) host.erase(colon);
    std::ranges::transform(host, host.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    auto configuredHost = cfg.host;
    if (const auto cfgColon = configuredHost.find(':'); cfgColon != std::string::npos)
        configuredHost.erase(cfgColon);
    std::ranges::transform(configuredHost, configuredHost.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (host.empty() || host == configuredHost || looksLikeIpAddressOrLocalhost(host)) return std::nullopt;
    const auto dot = host.find('.');
    if (dot == std::string::npos || dot == 0) return std::nullopt;
    return host.substr(0, dot);
}

Router::Response makeResponse(
    const Router::Request& request,
    const http::status status,
    std::string body = {},
    const std::string& contentType = "application/xml") {
    Router::Response response{status, request.version()};
    response.set(http::field::server, "VaulthallaS3Gateway");
    response.set(http::field::content_type, contentType);
    response.body() = std::move(body);
    response.keep_alive(request.keep_alive());
    response.prepare_payload();
    return response;
}

std::vector<uint8_t> bodyBytes(const Router::Request& request, const Router::BodyPayload& payload) {
    if (payload.temp_file) {
        std::ifstream in(*payload.temp_file, std::ios::binary);
        if (!in) throw invalidArgument("Unable to read streamed request body", payload.temp_file->string());
        return {std::istreambuf_iterator<char>(in), {}};
    }
    return {request.body().begin(), request.body().end()};
}

std::string routerMd5Base64(const std::vector<uint8_t>& bytes) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(bytes.data(), bytes.size(), digest);
    std::array<unsigned char, EVP_ENCODE_LENGTH(MD5_DIGEST_LENGTH)> encoded{};
    const auto len = EVP_EncodeBlock(encoded.data(), digest, MD5_DIGEST_LENGTH);
    return {reinterpret_cast<char*>(encoded.data()), static_cast<std::size_t>(len)};
}

std::map<std::string, std::string> userMetadata(const Router::Request& request) {
    std::map<std::string, std::string> out;
    for (const auto& field : request) {
        auto name = std::string(field.name_string());
        std::ranges::transform(name, name.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name.starts_with("x-amz-meta-"))
            out[name.substr(std::string("x-amz-meta-").size())] = std::string(field.value());
    }
    return out;
}

std::string headerOr(const Router::Request& request, const http::field field, const std::string& fallback = {}) {
    const auto it = request.find(field);
    return it == request.end() ? fallback : std::string(it->value());
}

std::string headerOrName(const Router::Request& request, const std::string& name, const std::string& fallback = {}) {
    const auto it = request.find(name);
    return it == request.end() ? fallback : std::string(it->value());
}

void validateContentMd5(const Router::Request& request, const Router::BodyPayload& payload) {
    const auto expected = headerOrName(request, "content-md5");
    if (expected.empty()) return;
    if (!payload.md5_base64)
        throw invalidArgument("Content-MD5 validation is unavailable for this request", std::string(request.target()));
    if (*payload.md5_base64 != expected)
        throw S3Error{"BadDigest", "The Content-MD5 header does not match the request body",
                      http::status::bad_request, std::string(request.target())};
}

void validateContentMd5(const Router::Request& request, const std::vector<uint8_t>& body) {
    const auto expected = headerOrName(request, "content-md5");
    if (expected.empty()) return;
    if (routerMd5Base64(body) != expected)
        throw S3Error{"BadDigest", "The Content-MD5 header does not match the request body",
                      http::status::bad_request, std::string(request.target())};
}

void validateChecksumHeader(
    const Router::Request& request,
    const std::string& header,
    const std::optional<std::string>& actual,
    const std::optional<std::string>& trailer = std::nullopt) {
    auto expected = headerOrName(request, header);
    if (expected.empty() && trailer) expected = *trailer;
    if (expected.empty()) return;
    if (!actual || *actual != expected)
        throw S3Error{"BadDigest", "The " + header + " header does not match the request body",
                      http::status::bad_request, std::string(request.target())};
}

void validateS3Checksums(const Router::Request& request, const Router::BodyPayload& payload) {
    const auto shaTrailer = payload.trailer_headers.find("x-amz-checksum-sha256");
    const auto crcTrailer = payload.trailer_headers.find("x-amz-checksum-crc32");
    std::optional<std::string> shaTrailerValue;
    std::optional<std::string> crcTrailerValue;
    if (shaTrailer != payload.trailer_headers.end()) shaTrailerValue = shaTrailer->second;
    if (crcTrailer != payload.trailer_headers.end()) crcTrailerValue = crcTrailer->second;
    validateChecksumHeader(
        request,
        "x-amz-checksum-sha256",
        payload.sha256_base64,
        shaTrailerValue);
    validateChecksumHeader(
        request,
        "x-amz-checksum-crc32",
        payload.crc32_base64,
        crcTrailerValue);
}

void validateS3Checksums(const Router::Request& request, const std::vector<uint8_t>& body) {
    if (!headerOrName(request, "x-amz-checksum-sha256").empty())
        validateChecksumHeader(request, "x-amz-checksum-sha256", Router::checksumSha256Base64(body));
    if (!headerOrName(request, "x-amz-checksum-crc32").empty())
        validateChecksumHeader(request, "x-amz-checksum-crc32", Router::checksumCrc32Base64(body));
}

std::string trimCopy(std::string value) {
    const auto notSpace = [](const unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::ranges::find_if(value, notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool etagListMatches(const std::string& condition, const std::string& etag) {
    std::stringstream ss(condition);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trimCopy(item);
        if (item == "*" || item == etag) return true;
        if (!item.empty() && item.front() != '"' && ("\"" + item + "\"") == etag) return true;
    }
    return false;
}

void enforceIfMatch(const Router::Request& request, const db::query::s3::ObjectState& state,
                    const std::string& resource) {
    const auto condition = headerOr(request, http::field::if_match);
    if (!condition.empty() && !etagListMatches(condition, state.etag))
        throw preconditionFailed(resource);
}

bool ifNoneMatchMatches(const Router::Request& request, const db::query::s3::ObjectState& state) {
    const auto condition = headerOr(request, http::field::if_none_match);
    return !condition.empty() && etagListMatches(condition, state.etag);
}

Router::Response notModifiedResponse(const Router::Request& request, const db::query::s3::ObjectState& state) {
    auto response = makeResponse(request, http::status::not_modified, {});
    response.set(http::field::etag, state.etag);
    response.set(http::field::last_modified, xml::httpDate(state.last_modified));
    response.body().clear();
    response.prepare_payload();
    return response;
}

void enforceCopySourcePreconditions(const Router::Request& request,
                                    const db::query::s3::ObjectState& state,
                                    const std::string& resource) {
    const auto ifMatch = headerOrName(request, "x-amz-copy-source-if-match");
    if (!ifMatch.empty() && !etagListMatches(ifMatch, state.etag))
        throw preconditionFailed(resource);

    const auto ifNoneMatch = headerOrName(request, "x-amz-copy-source-if-none-match");
    if (!ifNoneMatch.empty() && etagListMatches(ifNoneMatch, state.etag))
        throw preconditionFailed(resource);
}

PutObjectOptions putOptionsFromRequest(const Router::Request& request) {
    const auto contentType = headerOr(request, http::field::content_type);
    return {
        .content_type = contentType.empty() ? "application/octet-stream" : contentType,
        .content_type_explicit = !contentType.empty(),
        .storage_class = headerOrName(request, "x-amz-storage-class").empty()
            ? std::optional<std::string>{}
            : std::make_optional(headerOrName(request, "x-amz-storage-class")),
        .metadata = userMetadata(request),
        .metadata_directive = MetadataDirective::Replace,
        .multipart = false,
        .part_count = std::nullopt,
        .etag_override = std::nullopt
    };
}

PutObjectOptions copyOptionsFromRequest(const Router::Request& request) {
    auto options = putOptionsFromRequest(request);
    auto directive = headerOrName(request, "x-amz-metadata-directive", "COPY");
    std::ranges::transform(directive, directive.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    if (directive == "COPY") {
        options.metadata_directive = MetadataDirective::Copy;
        return options;
    }
    if (directive == "REPLACE") {
        options.metadata_directive = MetadataDirective::Replace;
        return options;
    }
    throw invalidArgument("Unsupported x-amz-metadata-directive", directive);
}

uint64_t parseRangeNumber(const std::string& value, const Router::Request& request) {
    if (value.empty() || !std::ranges::all_of(value, [](const unsigned char c) {
            return std::isdigit(c);
        }))
        throw invalidRange(std::string(request.target()));
    try {
        return std::stoull(value);
    } catch (const std::exception&) {
        throw invalidRange(std::string(request.target()));
    }
}

uint32_t parsePartNumber(const std::map<std::string, std::string>& query) {
    const auto it = query.find("partNumber");
    if (it == query.end() || it->second.empty() || !std::ranges::all_of(it->second, [](const unsigned char c) {
            return std::isdigit(c);
        }))
        throw invalidArgument("partNumber must be an integer between 1 and 10000", it == query.end() ? "" : it->second);
    try {
        const auto value = std::stoul(it->second);
        if (value == 0 || value > 10000)
            throw invalidArgument("partNumber must be between 1 and 10000", it->second);
        return static_cast<uint32_t>(value);
    } catch (const std::invalid_argument&) {
        throw invalidArgument("partNumber must be an integer between 1 and 10000", it->second);
    } catch (const std::out_of_range&) {
        throw invalidArgument("partNumber must be between 1 and 10000", it->second);
    }
}

std::optional<ByteRange> parseRange(const Router::Request& request) {
    const auto raw = headerOr(request, http::field::range);
    if (raw.empty()) return std::nullopt;
    if (!raw.starts_with("bytes=")) throw invalidRange(std::string(request.target()));
    if (raw.find(',', 6) != std::string::npos)
        throw notImplemented("Multipart byte ranges are not supported", std::string(request.target()));
    const auto dash = raw.find('-', 6);
    if (dash == std::string::npos) throw invalidRange(std::string(request.target()));

    const auto firstRaw = raw.substr(6, dash - 6);
    const auto lastRaw = raw.substr(dash + 1);
    if (firstRaw.empty() && lastRaw.empty()) throw invalidRange(std::string(request.target()));

    ByteRange range;
    if (!firstRaw.empty()) range.first = parseRangeNumber(firstRaw, request);
    if (!lastRaw.empty()) range.last = parseRangeNumber(lastRaw, request);
    if (range.first && range.last && *range.last < *range.first) throw invalidRange(std::string(request.target()));
    return range;
}

uint32_t parseMaxKeys(const std::map<std::string, std::string>& query) {
    const auto it = query.find("max-keys");
    if (it == query.end()) return 1000;
    if (it->second.empty() || !std::ranges::all_of(it->second, [](const unsigned char c) {
            return std::isdigit(c);
        }))
        throw invalidArgument("max-keys must be a non-negative integer", it->second);

    try {
        const auto value = std::stoull(it->second);
        if (value > std::numeric_limits<uint32_t>::max())
            throw invalidArgument("max-keys is too large", it->second);
        return static_cast<uint32_t>(value);
    } catch (const std::invalid_argument&) {
        throw invalidArgument("max-keys must be a non-negative integer", it->second);
    } catch (const std::out_of_range&) {
        throw invalidArgument("max-keys is too large", it->second);
    }
}

std::optional<std::string> parseEncodingType(const std::map<std::string, std::string>& query) {
    const auto it = query.find("encoding-type");
    if (it == query.end() || it->second.empty()) return std::nullopt;
    if (it->second == "url") return it->second;
    throw invalidArgument("encoding-type must be url when specified", it->second);
}

std::vector<std::string> parseDeleteObjects(const std::string& body, bool& quiet) {
    pugi::xml_document doc;
    if (!doc.load_string(body.c_str())) throw invalidArgument("Malformed DeleteObjects XML");
    std::vector<std::string> out;
    const auto root = doc.child("Delete");
    quiet = std::string(root.child("Quiet").text().as_string()) == "true";
    for (const auto object : root.children("Object")) {
        const auto key = object.child("Key").text().as_string();
        if (key && *key) out.emplace_back(key);
    }
    return out;
}

std::vector<std::pair<uint32_t, std::string>> parseCompleteParts(const std::string& body) {
    pugi::xml_document doc;
    if (!doc.load_string(body.c_str())) throw invalidArgument("Malformed CompleteMultipartUpload XML");
    std::vector<std::pair<uint32_t, std::string>> out;
    for (const auto part : doc.child("CompleteMultipartUpload").children("Part")) {
        auto etag = std::string(part.child("ETag").text().as_string());
        if (!etag.empty() && etag.front() != '"') etag = "\"" + etag + "\"";
        out.emplace_back(part.child("PartNumber").text().as_uint(), etag);
    }
    return out;
}

std::string copyObjectResult(const std::string& etag, const std::time_t lastModified) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<CopyObjectResult><LastModified>" + xml::iso8601(lastModified) + "</LastModified>"
           "<ETag>" + xml::escape(etag) + "</ETag></CopyObjectResult>";
}

xml::DeleteError deleteErrorFromMessage(const std::string& key, const std::string& raw) {
    const auto split = raw.find(": ");
    if (split == std::string::npos)
        return {.key = key, .code = "InternalError", .message = raw};
    return {
        .key = key,
        .code = raw.substr(0, split),
        .message = raw.substr(split + 2)
    };
}

struct GatewayBudgetReservation {
    storage::s3::pricing::PriceBudgetPreflightRequest request;
    storage::s3::pricing::S3GatewayPriceEstimate estimate;
    storage::s3::pricing::PriceBudgetDecision decision;
};

std::pair<std::string, bool> providerBudgetIdentity(const std::shared_ptr<storage::CloudEngine>& cloud) {
    const auto profile = cloud ? cloud->s3ProviderProfile() : nullptr;
    const auto costProfileId = profile ? profile->costProfileId() : std::optional<std::string>{};
    return {
        costProfileId ? *costProfileId : (profile ? profile->id() : std::string{"unknown"}),
        costProfileId && storage::s3::pricing::isSupportedPriceBudgetProvider(*costProfileId)
    };
}

std::optional<GatewayBudgetReservation> preflightGatewayBudget(
    const ResolvedBucket& bucket,
    const AuthContext& auth,
    const std::string& rid,
    const storage::s3::pricing::S3GatewayOperation operation,
    const uint64_t uploadBytes = 0,
    const uint64_t downloadBytes = 0,
    const uint64_t objectCount = 1,
    const std::optional<std::string>& storageClass = std::nullopt,
    const std::optional<std::string>& objectKey = std::nullopt) {
    if (!ObjectStore::isRemoteBacked(bucket)) return std::nullopt;

    const auto cloud = ObjectStore::cloudEngine(bucket);
    if (!cloud) throw invalidArgument("Bucket is not backed by a CloudEngine", bucket.bucket_name);

    const auto [providerKey, providerSupported] = providerBudgetIdentity(cloud);
    storage::s3::pricing::S3GatewayPriceEstimateRequest estimateRequest{
        .vault_id = bucket.vault_id,
        .gateway_credential_id = auth.credential_id == 0 ? std::optional<uint32_t>{} : std::make_optional(auth.credential_id),
        .provider_key = providerKey,
        .provider_supported = providerSupported,
        .operation = operation,
        .request_count = 1,
        .upload_bytes = uploadBytes,
        .download_bytes = downloadBytes,
        .object_count = objectCount,
        .storage_class = storageClass
    };
    auto estimate = storage::s3::pricing::estimateGatewayS3Request(*cloud, estimateRequest);

    storage::s3::pricing::PriceBudgetPreflightRequest budgetRequest{
        .vault_id = bucket.vault_id,
        .run_uuid = rid,
        .provider_key = providerKey,
        .provider_supported = providerSupported,
        .estimate = estimate.as_price_estimate_report,
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = auth.credential_id == 0 ? std::optional<uint32_t>{} : std::make_optional(auth.credential_id),
        .request_uuid = rid,
        .operation = storage::s3::pricing::toString(operation),
        .object_key = objectKey
    };

    storage::s3::pricing::PriceBudgetService service;
    auto decision = service.preflight(budgetRequest);
    service.recordPreflightNotifications(budgetRequest, decision);
    if (!decision.allowed) {
        throw S3Error{
            "AccessDenied",
            "S3 gateway price budget would be exceeded: " +
                (decision.reason.empty() ? std::string{"request denied by price budget policy"} : decision.reason),
            http::status::forbidden,
            bucket.bucket_name};
    }

    return GatewayBudgetReservation{
        .request = std::move(budgetRequest),
        .estimate = std::move(estimate),
        .decision = std::move(decision)
    };
}

void commitGatewayBudget(const std::optional<GatewayBudgetReservation>& budget, const bool finalCostKnown = true) {
    if (!budget) return;
    storage::s3::pricing::PriceBudgetService{}.commit(
        budget->decision.reservations,
        finalCostKnown && budget->estimate.available
            ? std::make_optional(budget->estimate.estimated_cost)
            : std::optional<std::string>{});
}

void releaseGatewayBudget(const std::optional<GatewayBudgetReservation>& budget) {
    if (!budget) return;
    storage::s3::pricing::PriceBudgetService{}.release(budget->decision.reservations);
}
}

Router::Router()
    : auth_(config::Registry::get().s3_gateway.require_sigv4) {}

std::string Router::checksumSha256Base64(const std::vector<uint8_t>& bytes) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(bytes.data(), bytes.size(), digest);
    std::array<unsigned char, EVP_ENCODE_LENGTH(SHA256_DIGEST_LENGTH)> encoded{};
    const auto len = EVP_EncodeBlock(encoded.data(), digest, SHA256_DIGEST_LENGTH);
    return {reinterpret_cast<char*>(encoded.data()), static_cast<std::size_t>(len)};
}

std::string Router::checksumCrc32Base64(const std::vector<uint8_t>& bytes) {
    const auto crc = static_cast<uint32_t>(crc32(0L, bytes.data(), static_cast<uInt>(bytes.size())));
    const std::array<unsigned char, 4> raw{
        static_cast<unsigned char>((crc >> 24) & 0xff),
        static_cast<unsigned char>((crc >> 16) & 0xff),
        static_cast<unsigned char>((crc >> 8) & 0xff),
        static_cast<unsigned char>(crc & 0xff)
    };
    std::array<unsigned char, EVP_ENCODE_LENGTH(raw.size())> encoded{};
    const auto len = EVP_EncodeBlock(encoded.data(), raw.data(), static_cast<int>(raw.size()));
    return {reinterpret_cast<char*>(encoded.data()), static_cast<std::size_t>(len)};
}

Router::ParsedTarget Router::parseRequestTarget(const Request& request) {
    const auto path = targetPath(std::string(request.target()));
    const auto query = parseQuery(targetQuery(std::string(request.target())));
    std::string decoded = pctDecode(path);
    while (!decoded.empty() && decoded.front() == '/') decoded.erase(decoded.begin());

    ParsedTarget out;
    out.query = query;
    if (const auto virtualBucket = virtualHostedBucket(request)) {
        out.bucket = *virtualBucket;
        out.key = decoded;
        return out;
    }

    if (!config::Registry::get().s3_gateway.allow_path_style)
        throw invalidArgument("Path-style bucket addressing is disabled", path);

    const auto slash = decoded.find('/');
    if (slash == std::string::npos) {
        out.bucket = decoded;
        return out;
    }
    out.bucket = decoded.substr(0, slash);
    out.key = decoded.substr(slash + 1);
    return out;
}

Router::ParsedCopySource Router::parseCopySource(std::string raw) {
    while (!raw.empty() && raw.front() == '/') raw.erase(raw.begin());

    const auto queryStart = raw.find('?');
    const auto rawPath = queryStart == std::string::npos ? raw : raw.substr(0, queryStart);
    const auto rawQuery = queryStart == std::string::npos ? std::string{} : raw.substr(queryStart + 1);

    const auto slash = rawPath.find('/');
    if (slash == std::string::npos || slash == 0)
        throw invalidArgument("Invalid copy source", raw);

    return {
        .bucket = pctDecode(rawPath.substr(0, slash)),
        .key = pctDecode(rawPath.substr(slash + 1)),
        .query = parseQuery(rawQuery)
    };
}

Router::Response Router::route(Request&& request) const {
    return route(std::move(request), BodyPayload{});
}

Router::Response Router::route(Request&& request, BodyPayload payload) const {
    const auto rid = requestId();
    try {
        auto input = sigv4::inputFromRequest(request, request.body());
        input.body_sha256 = payload.sha256_hex;
        const auto auth = auth_.authenticate(input);
        auto response = routeAuthenticated(std::move(request), auth, rid, payload);
        response.set("x-amz-request-id", rid);
        return response;
    } catch (const S3Error& e) {
        return errorResponse(request, e, rid);
    } catch (const std::exception& e) {
        log::Registry::runtime()->error("[S3Gateway] Request failed: {}", e.what());
        return errorResponse(request, S3Error{"InternalError", e.what(), http::status::internal_server_error, std::string(request.target())}, rid);
    }
}

Router::Response Router::errorResponse(const Request& request, const S3Error& error, const std::string& rid) {
    auto response = makeResponse(
        request,
        error.status,
        xml::error(error.code, error.what(), error.resource, rid));
    response.set("x-amz-request-id", rid);
    return response;
}

Router::Response Router::routeAuthenticated(
    Request&& request,
    const AuthContext& auth,
    const std::string& rid,
    const BodyPayload& payload) const {
    const auto parsed = parseRequestTarget(request);

    if (parsed.bucket.empty()) {
        if (request.method() != http::verb::get) throw notImplemented("Only GET / is supported at service root", "/");
        std::vector<xml::Bucket> buckets;
        for (const auto& bucket : objects_.listBuckets(auth))
            buckets.push_back({.name = bucket.bucket_name, .created_at = bucket.created_at});
        return makeResponse(request, http::status::ok, xml::listBuckets(buckets, auth.user->name));
    }

    if (parsed.key.empty()) {
        if (request.method() == http::verb::put) {
            objects_.createBucket(parsed.bucket, auth, config::Registry::get().s3_gateway.default_bucket_mode);
            return makeResponse(request, http::status::ok, {});
        }
        if (request.method() == http::verb::head) {
            auto bucket = objects_.headBucket(parsed.bucket, auth);
            auto budget = preflightGatewayBudget(bucket, auth, rid, storage::s3::pricing::S3GatewayOperation::HeadBucket);
            commitGatewayBudget(budget);
            auto response = makeResponse(request, http::status::ok, {});
            response.body().clear();
            response.prepare_payload();
            return response;
        }
        if (request.method() == http::verb::delete_) {
            auto bucket = objects_.resolveBucket(parsed.bucket, auth);
            objects_.requireBucketRbacPermission(bucket, Action::Delete);
            if (!ObjectStore::credentialAllowsAdmin(bucket)) throw accessDenied(parsed.bucket);
            auto budget = preflightGatewayBudget(bucket, auth, rid, storage::s3::pricing::S3GatewayOperation::DeleteBucket);
            try {
                objects_.deleteBucket(parsed.bucket, auth);
                commitGatewayBudget(budget);
            } catch (...) {
                releaseGatewayBudget(budget);
                throw;
            }
            return makeResponse(request, http::status::no_content, {});
        }

        auto bucket = objects_.headBucket(parsed.bucket, auth);
        if (request.method() == http::verb::get && hasQuery(parsed.query, "uploads"))
        {
            auto budget = preflightGatewayBudget(bucket, auth, rid, storage::s3::pricing::S3GatewayOperation::ListMultipartUploads);
            try {
                auto response = makeResponse(request, http::status::ok, xml::listMultipartUploads(parsed.bucket, multipart_.listUploads(bucket, parsed.query.contains("prefix") ? parsed.query.at("prefix") : "")));
                commitGatewayBudget(budget);
                return response;
            } catch (...) {
                releaseGatewayBudget(budget);
                throw;
            }
        }

        if (request.method() == http::verb::get) {
            db::query::s3::ObjectListParams params;
            params.prefix = parsed.query.contains("prefix") ? parsed.query.at("prefix") : "";
            if (parsed.query.contains("delimiter")) params.delimiter = parsed.query.at("delimiter");
            if (parsed.query.contains("start-after")) params.start_after = parsed.query.at("start-after");
            if (parsed.query.contains("continuation-token")) params.continuation_token = parsed.query.at("continuation-token");
            params.max_keys = parseMaxKeys(parsed.query);
            const auto encodingType = parseEncodingType(parsed.query);
            auto budget = preflightGatewayBudget(bucket, auth, rid, storage::s3::pricing::S3GatewayOperation::ListObjectsV2);
            try {
                auto result = objects_.listObjects(bucket, params);
                auto response = makeResponse(
                    request,
                    http::status::ok,
                    xml::listObjectsV2(parsed.bucket, result, params.prefix, params.delimiter, params.max_keys, encodingType));
                if (objects_.remoteIndexStale(bucket))
                    response.set("x-vaulthalla-index-stale", "true");
                commitGatewayBudget(budget);
                return response;
            } catch (...) {
                releaseGatewayBudget(budget);
                throw;
            }
        }

        if (request.method() == http::verb::post && hasQuery(parsed.query, "delete")) {
            bool quiet = false;
            const auto keys = parseDeleteObjects(request.body(), quiet);
            for (const auto& key : keys)
                objects_.requireObjectPermission(bucket, key, Action::Delete);
            auto budget = preflightGatewayBudget(
                bucket,
                auth,
                rid,
                storage::s3::pricing::S3GatewayOperation::DeleteObjects,
                0,
                0,
                keys.size());
            std::vector<std::pair<std::string, std::optional<std::string>>> results;
            try {
                results = objects_.deleteObjects(bucket, keys);
            } catch (...) {
                commitGatewayBudget(budget, false);
                throw;
            }
            std::vector<xml::DeletedObject> deleted;
            std::vector<xml::DeleteError> errors;
            for (const auto& [key, error] : results) {
                if (!error) deleted.push_back({.key = key});
                else errors.push_back(deleteErrorFromMessage(key, *error));
            }
            auto response = makeResponse(request, http::status::ok, xml::deleteResult(deleted, errors, quiet));
            commitGatewayBudget(budget);
            return response;
        }

        throw notImplemented("Unsupported bucket operation", parsed.bucket);
    }

    auto bucket = objects_.resolveBucket(parsed.bucket, auth);

    if (request.method() == http::verb::post && hasQuery(parsed.query, "uploads")) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Write);
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::CreateMultipartUpload,
            0,
            0,
            1,
            putOptionsFromRequest(request).storage_class,
            parsed.key);
        try {
            const auto uploadId = multipart_.createUpload(bucket, parsed.key, putOptionsFromRequest(request));
            commitGatewayBudget(budget);
            return makeResponse(request, http::status::ok, xml::initiateMultipartUpload(parsed.bucket, parsed.key, uploadId));
        } catch (...) {
            releaseGatewayBudget(budget);
            throw;
        }
    }

    if (request.method() == http::verb::put && hasQuery(parsed.query, "partNumber") && hasQuery(parsed.query, "uploadId")) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Write);
        const auto partNumber = parsePartNumber(parsed.query);
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::UploadPart,
            payload.size,
            0,
            1,
            std::nullopt,
            parsed.key);
        db::query::s3::MultipartPart part;
        try {
            if (payload.temp_file) {
                validateContentMd5(request, payload);
                validateS3Checksums(request, payload);
                part = multipart_.uploadPartFromFile(bucket, parsed.key, parsed.query.at("uploadId"), partNumber,
                                                     *payload.temp_file, payload.size);
            } else {
                auto body = bodyBytes(request, payload);
                validateContentMd5(request, body);
                validateS3Checksums(request, body);
                part = multipart_.uploadPart(bucket, parsed.key, parsed.query.at("uploadId"), partNumber, body);
            }
            commitGatewayBudget(budget);
        } catch (...) {
            releaseGatewayBudget(budget);
            throw;
        }
        auto response = makeResponse(request, http::status::ok, {});
        response.set(http::field::etag, part.etag);
        return response;
    }

    if (request.method() == http::verb::post && hasQuery(parsed.query, "uploadId")) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Write);
        uint64_t totalSize = 0;
        for (const auto& part : multipart_.listParts(bucket, parsed.key, parsed.query.at("uploadId")))
            totalSize += part.size_bytes;
        const auto completeParts = parseCompleteParts(request.body());
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::CompleteMultipartUpload,
            totalSize,
            0,
            1,
            std::nullopt,
            parsed.key);
        try {
            const auto state = multipart_.completeUpload(bucket, parsed.key, parsed.query.at("uploadId"), completeParts);
            commitGatewayBudget(budget);
            return makeResponse(
                request,
                http::status::ok,
                xml::completeMultipartUpload("/" + parsed.bucket + "/" + parsed.key, parsed.bucket, parsed.key, state.etag));
        } catch (...) {
            commitGatewayBudget(budget, false);
            throw;
        }
    }

    if (request.method() == http::verb::delete_ && hasQuery(parsed.query, "uploadId")) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Delete);
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::AbortMultipartUpload,
            0,
            0,
            1,
            std::nullopt,
            parsed.key);
        try {
            multipart_.abortUpload(bucket, parsed.key, parsed.query.at("uploadId"));
            commitGatewayBudget(budget);
            return makeResponse(request, http::status::no_content, {});
        } catch (...) {
            releaseGatewayBudget(budget);
            throw;
        }
    }

    if (request.method() == http::verb::get && hasQuery(parsed.query, "uploadId")) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Read);
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::ListParts,
            0,
            0,
            1,
            std::nullopt,
            parsed.key);
        try {
            auto response = makeResponse(
                request,
                http::status::ok,
                xml::listParts(parsed.bucket, parsed.key, parsed.query.at("uploadId"),
                               multipart_.listParts(bucket, parsed.key, parsed.query.at("uploadId"))));
            commitGatewayBudget(budget);
            return response;
        } catch (...) {
            releaseGatewayBudget(budget);
            throw;
        }
    }

    if (request.method() == http::verb::head) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Read);
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::HeadObject,
            0,
            0,
            1,
            std::nullopt,
            parsed.key);
        db::query::s3::ObjectState state;
        try {
            state = objects_.headObject(bucket, parsed.key);
            enforceIfMatch(request, state, parsed.key);
            if (ifNoneMatchMatches(request, state)) {
                commitGatewayBudget(budget);
                return notModifiedResponse(request, state);
            }
            const auto objectKey = ObjectStore::vaultPathToKey(ObjectStore::keyToVaultPath(parsed.key));
            const auto metadata = db::query::s3::Gateway::listObjectMetadata(bucket.vault_id, objectKey);
            auto response = makeResponse(request, http::status::ok, {});
            response.set(http::field::etag, state.etag);
            response.set(http::field::content_length, std::to_string(state.size_bytes));
            response.set(http::field::content_type, state.content_type.value_or("application/octet-stream"));
            response.set(http::field::last_modified, xml::httpDate(state.last_modified));
            for (const auto& [name, value] : metadata)
                response.set("x-amz-meta-" + name, value);
            // Do not call prepare_payload() again: HEAD must advertise object size, not empty-body size.
            response.body().clear();
            commitGatewayBudget(budget);
            return response;
        } catch (...) {
            releaseGatewayBudget(budget);
            throw;
        }
    }

    if (request.method() == http::verb::get) {
        const auto state = objects_.headObject(bucket, parsed.key);
        enforceIfMatch(request, state, parsed.key);
        if (ifNoneMatchMatches(request, state)) return notModifiedResponse(request, state);
        const auto range = parseRange(request);
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::GetObject,
            0,
            state.size_bytes,
            1,
            state.storage_class,
            parsed.key);
        ObjectBody object;
        try {
            object = objects_.getObject(bucket, parsed.key, range);
        } catch (...) {
            commitGatewayBudget(budget, false);
            throw;
        }
        auto response = makeResponse(
            request,
            range ? http::status::partial_content : http::status::ok,
            std::string(object.bytes.begin(), object.bytes.end()),
            object.state.content_type.value_or("application/octet-stream"));
        response.set(http::field::etag, object.state.etag);
        response.set(http::field::last_modified, xml::httpDate(object.state.last_modified));
        if (object.content_range) {
            const auto [first, last] = *object.content_range;
            response.set(http::field::content_range,
                         "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" +
                         std::to_string(object.state.size_bytes));
        }
        for (const auto& [name, value] : object.metadata)
            response.set("x-amz-meta-" + name, value);
        response.prepare_payload();
        commitGatewayBudget(budget);
        return response;
    }

    if (request.method() == http::verb::put) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Write);
        const auto ifMatch = headerOr(request, http::field::if_match);
        const auto ifNoneMatch = headerOr(request, http::field::if_none_match);
        if (!ifMatch.empty()) {
            try {
                const auto current = objects_.headObject(bucket, parsed.key);
                enforceIfMatch(request, current, parsed.key);
            } catch (const S3Error& e) {
                if (e.code == "NoSuchKey") throw preconditionFailed(parsed.key);
                throw;
            }
        }
        if (!ifNoneMatch.empty()) {
            try {
                const auto current = objects_.headObject(bucket, parsed.key);
                if (etagListMatches(ifNoneMatch, current.etag)) throw preconditionFailed(parsed.key);
            } catch (const S3Error& e) {
                if (e.code != "NoSuchKey") throw;
            }
        }

        const auto copySource = headerOrName(request, "x-amz-copy-source");
        if (!copySource.empty()) {
            const auto source = parseCopySource(copySource);
            if (source.query.contains("versionId"))
                throw notImplemented("Versioned CopyObject sources are not supported", copySource);
            const auto sourceBucket = objects_.resolveBucket(source.bucket, auth);
            const auto sourceState = objects_.headObject(sourceBucket, source.key);
            enforceCopySourcePreconditions(
                request,
                sourceState,
                source.bucket + "/" + source.key);
            const auto copyOptions = copyOptionsFromRequest(request);
            const auto sameRemoteVault =
                ObjectStore::isRemoteBacked(sourceBucket) &&
                ObjectStore::isRemoteBacked(bucket) &&
                sourceBucket.vault_id == bucket.vault_id;
            std::optional<GatewayBudgetReservation> sourceBudget;
            std::optional<GatewayBudgetReservation> destBudget;
            bool upstreamMayHaveStarted = false;
            try {
                if (sameRemoteVault) {
                    destBudget = preflightGatewayBudget(
                        bucket,
                        auth,
                        rid,
                        storage::s3::pricing::S3GatewayOperation::CopyObject,
                        sourceState.size_bytes,
                        sourceState.size_bytes,
                        1,
                        copyOptions.storage_class,
                        parsed.key);
                } else {
                    sourceBudget = preflightGatewayBudget(
                        sourceBucket,
                        auth,
                        rid,
                        storage::s3::pricing::S3GatewayOperation::GetObject,
                        0,
                        sourceState.size_bytes,
                        1,
                        sourceState.storage_class,
                        source.key);
                    destBudget = preflightGatewayBudget(
                        bucket,
                        auth,
                        rid,
                        storage::s3::pricing::S3GatewayOperation::PutObject,
                        sourceState.size_bytes,
                        0,
                        1,
                        copyOptions.storage_class,
                        parsed.key);
                }
                upstreamMayHaveStarted = true;
                const auto state = objects_.copyObject(
                    sourceBucket,
                    source.key,
                    bucket,
                    parsed.key,
                    copyOptions);
                commitGatewayBudget(sourceBudget);
                commitGatewayBudget(destBudget);
                return makeResponse(request, http::status::ok, copyObjectResult(state.etag, state.last_modified));
            } catch (...) {
                if (upstreamMayHaveStarted) {
                    commitGatewayBudget(sourceBudget, false);
                    commitGatewayBudget(destBudget, false);
                } else {
                    releaseGatewayBudget(sourceBudget);
                    releaseGatewayBudget(destBudget);
                }
                throw;
            }
        }
        db::query::s3::ObjectState state;
        const auto options = putOptionsFromRequest(request);
        std::optional<std::vector<uint8_t>> bufferedBody;
        if (payload.temp_file) {
            validateContentMd5(request, payload);
            validateS3Checksums(request, payload);
        } else {
            bufferedBody = bodyBytes(request, payload);
            validateContentMd5(request, *bufferedBody);
            validateS3Checksums(request, *bufferedBody);
        }
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::PutObject,
            payload.temp_file ? payload.size : bufferedBody->size(),
            0,
            1,
            options.storage_class,
            parsed.key);
        if (payload.temp_file) {
            auto fileOptions = options;
            if (payload.md5_hex) fileOptions.etag_override = "\"" + *payload.md5_hex + "\"";
            try {
                state = objects_.putObjectFromFile(bucket, parsed.key, *payload.temp_file, payload.size, fileOptions);
                commitGatewayBudget(budget);
            } catch (...) {
                commitGatewayBudget(budget, false);
                throw;
            }
        } else {
            try {
                state = objects_.putObject(bucket, parsed.key, *bufferedBody, options);
                commitGatewayBudget(budget);
            } catch (...) {
                commitGatewayBudget(budget, false);
                throw;
            }
        }
        auto response = makeResponse(request, http::status::ok, {});
        response.set(http::field::etag, state.etag);
        return response;
    }

    if (request.method() == http::verb::delete_) {
        objects_.requireObjectPermission(bucket, parsed.key, Action::Delete);
        const auto ifMatch = headerOr(request, http::field::if_match);
        if (!ifMatch.empty()) {
            try {
                const auto current = objects_.headObject(bucket, parsed.key);
                enforceIfMatch(request, current, parsed.key);
            } catch (const S3Error& e) {
                if (e.code == "NoSuchKey") throw preconditionFailed(parsed.key);
                throw;
            }
        }
        auto budget = preflightGatewayBudget(
            bucket,
            auth,
            rid,
            storage::s3::pricing::S3GatewayOperation::DeleteObject,
            0,
            0,
            1,
            std::nullopt,
            parsed.key);
        try {
            objects_.deleteObject(bucket, parsed.key);
            commitGatewayBudget(budget);
        } catch (...) {
            commitGatewayBudget(budget, false);
            throw;
        }
        return makeResponse(request, http::status::no_content, {});
    }

    throw notImplemented("Unsupported object operation", parsed.bucket + "/" + parsed.key);
}

} // namespace vh::protocols::s3
