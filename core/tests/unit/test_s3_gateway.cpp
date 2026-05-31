#include "db/Transactions.hpp"
#include "db/query/s3/Gateway.hpp"
#include "concurrency/ThreadPoolManager.hpp"
#include "config/Config.hpp"
#include "config/Registry.hpp"
#include "config/config_yaml.hpp"
#include "protocols/s3/MultipartStore.hpp"
#include "protocols/s3/ObjectStore.hpp"
#include "protocols/s3/GatewayService.hpp"
#include "protocols/s3/Router.hpp"
#include "protocols/s3/SigV4.hpp"
#include "protocols/s3/Xml.hpp"
#include "seed/include/init_db_tables.hpp"
#include "storage/CloudEngine.hpp"
#include "sync/model/RemotePolicy.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <paths.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

struct ConfigRestore {
    vh::config::Config previous;

    explicit ConfigRestore(vh::config::Config cfg)
        : previous(std::move(cfg)) {}

    ~ConfigRestore() {
        vh::config::Registry::set(previous);
    }
};

struct ThreadPoolShutdown {
    bool active = false;

    ~ThreadPoolShutdown() {
        if (active)
            vh::concurrency::ThreadPoolManager::instance().shutdown();
    }
};

uint16_t freeLoopbackPort() {
    boost::asio::io_context ioc;
    tcp::acceptor acceptor(ioc, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
    return acceptor.local_endpoint().port();
}

http::response<http::string_body> httpGetRoot(const uint16_t port) {
    boost::asio::io_context ioc;
    tcp::socket socket(ioc);
    socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), port));

    timeval timeout{
        .tv_sec = 5,
        .tv_usec = 0
    };
    (void)::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    req.set(http::field::user_agent, "vaulthalla-s3-gateway-test");
    req.prepare_payload();

    http::write(socket, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(socket, buffer, res);
    return res;
}

std::string amzNow() {
    const auto now = std::chrono::system_clock::now();
    const auto ts = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return out.str();
}

std::string amzAfter(const std::chrono::seconds offset) {
    const auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + offset);
    std::tm tm{};
    gmtime_r(&ts, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
    return out.str();
}

std::string scopeDate(const std::string& amzDate) {
    return amzDate.substr(0, 8);
}

std::vector<uint8_t> hexBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return out;
}

} // namespace

TEST(S3GatewayConfigTest, DefaultsToDisabledFiveGiBBodyLimit) {
    const vh::config::S3GatewayConfig cfg;

    EXPECT_FALSE(cfg.enabled);
    EXPECT_EQ(cfg.host, "0.0.0.0");
    EXPECT_EQ(cfg.port, 39000);
    EXPECT_EQ(cfg.max_body_size_bytes, 5ull * 1024ull * 1024ull * 1024ull);
    EXPECT_TRUE(cfg.require_sigv4);
    EXPECT_EQ(cfg.default_bucket_mode, "local");
    EXPECT_TRUE(cfg.default_api_exclusive);
    EXPECT_EQ(cfg.default_remote_sync_strategy, "cache");
    EXPECT_EQ(cfg.default_remote_conflict_policy, "keep_local");
}

TEST(S3GatewayConfigTest, YamlMapsBodyLimitMbAndClampsMultipartValues) {
    const auto node = YAML::Load(R"yaml(
enabled: true
host: 127.0.0.1
port: 39123
max_body_size_mb: 64
require_sigv4: false
allow_path_style: false
allow_virtual_hosted_style: true
multipart:
  part_dir: /tmp/vh-s3-parts
  min_part_size_mb: 1
  abort_after_days: 0
)yaml");

    const auto cfg = node.as<vh::config::S3GatewayConfig>();

    EXPECT_TRUE(cfg.enabled);
    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.port, 39123);
    EXPECT_EQ(cfg.max_body_size_bytes, 64ull * 1024ull * 1024ull);
    EXPECT_FALSE(cfg.require_sigv4);
    EXPECT_FALSE(cfg.allow_path_style);
    EXPECT_TRUE(cfg.allow_virtual_hosted_style);
    EXPECT_EQ(cfg.multipart.part_dir, "/tmp/vh-s3-parts");
    EXPECT_EQ(cfg.multipart.min_part_size_mb, 5u);
    EXPECT_EQ(cfg.multipart.abort_after_days, 1u);
}

TEST(S3GatewayConfigTest, JsonAndYamlPreferExplicitBytesOverMegabytes) {
    const nlohmann::json raw = {
        {"max_body_size_bytes", 12345},
        {"max_body_size_mb", 64}
    };
    const auto jsonCfg = raw.get<vh::config::S3GatewayConfig>();
    EXPECT_EQ(jsonCfg.max_body_size_bytes, 12345u);

    const auto yamlCfg = YAML::Load(R"yaml(
max_body_size_bytes: 54321
max_body_size_mb: 64
)yaml").as<vh::config::S3GatewayConfig>();
    EXPECT_EQ(yamlCfg.max_body_size_bytes, 54321u);
}

TEST(S3GatewayMultipartTest, PartRootUsesConfiguredDirectory) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.multipart.part_dir = "/tmp/vh-configured-s3-parts";
    vh::config::Registry::set(cfg);

    EXPECT_EQ(vh::protocols::s3::MultipartStore::partRoot(), "/tmp/vh-configured-s3-parts");
}

TEST(S3GatewayXmlTest, EscapesXmlReservedCharacters) {
    EXPECT_EQ(vh::protocols::s3::xml::escape("a&b<c>d\"e'f"),
              "a&amp;b&lt;c&gt;d&quot;e&apos;f");
}

TEST(S3GatewayXmlTest, DeleteResultHonorsQuietModeAndErrors) {
    using namespace vh::protocols::s3::xml;
    const auto xml = deleteResult({DeletedObject{.key = "ok.txt"}},
                                  {DeleteError{.key = "bad&.txt", .code = "AccessDenied", .message = "no <delete>"}},
                                  true);
    EXPECT_EQ(xml.find("<Deleted>"), std::string::npos);
    EXPECT_NE(xml.find("<Key>bad&amp;.txt</Key>"), std::string::npos);
    EXPECT_NE(xml.find("<Message>no &lt;delete&gt;</Message>"), std::string::npos);
}

TEST(S3GatewayXmlTest, ListObjectsV2UrlEncodesKeyFieldsWhenRequested) {
    vh::db::query::s3::ObjectListResult result;
    result.objects.push_back({
        .vault_id = 1,
        .object_key = "folder/a b&<.txt",
        .etag = "\"etag\"",
        .size_bytes = 3,
        .content_type = "text/plain",
        .storage_class = "STANDARD",
        .last_modified = 0,
        .multipart = false,
        .part_count = std::nullopt
    });
    result.common_prefixes.push_back("folder/sub dir/");
    result.next_continuation_token = "folder/next key&.txt";
    result.is_truncated = true;

    const auto xml = vh::protocols::s3::xml::listObjectsV2(
        "bucket",
        result,
        "folder/",
        std::make_optional<std::string>("/"),
        1000,
        std::make_optional<std::string>("url"));

    EXPECT_NE(xml.find("<EncodingType>url</EncodingType>"), std::string::npos);
    EXPECT_NE(xml.find("<Prefix>folder%2F</Prefix>"), std::string::npos);
    EXPECT_NE(xml.find("<Delimiter>%2F</Delimiter>"), std::string::npos);
    EXPECT_NE(xml.find("<Key>folder%2Fa%20b%26%3C.txt</Key>"), std::string::npos);
    EXPECT_NE(xml.find("<CommonPrefixes><Prefix>folder%2Fsub%20dir%2F</Prefix></CommonPrefixes>"), std::string::npos);
    EXPECT_NE(xml.find("<NextContinuationToken>folder%2Fnext%20key%26.txt</NextContinuationToken>"), std::string::npos);
}

TEST(S3GatewayObjectStoreTest, ComputesS3StyleEtags) {
    using vh::protocols::s3::ObjectStore;
    const std::vector<uint8_t> hello{'h', 'e', 'l', 'l', 'o'};

    EXPECT_EQ(ObjectStore::md5Hex(hello), "5d41402abc4b2a76b9719d911017c592");
    EXPECT_EQ(ObjectStore::multipartEtag({
                  hexBytes("5d41402abc4b2a76b9719d911017c592"),
                  hexBytes("7d793037a0760186574b0282f2f435e7")
              }),
              "\"065947336a2f2a95ba8899f3675c3be6-2\"");
}

TEST(S3GatewayObjectStoreTest, RejectsTraversalObjectKeys) {
    using vh::protocols::s3::ObjectStore;

    EXPECT_EQ(ObjectStore::keyToVaultPath("nested/object.txt"), "/nested/object.txt");
    EXPECT_THROW((void)ObjectStore::keyToVaultPath("../escape.txt"), std::runtime_error);
    EXPECT_THROW((void)ObjectStore::keyToVaultPath("nested/../../escape.txt"), std::runtime_error);
}

TEST(S3GatewayObjectStoreTest, PreservesDirectoryMarkerTrailingSlash) {
    using vh::protocols::s3::ObjectStore;

    const auto vaultPath = ObjectStore::keyToVaultPath("folder/");
    EXPECT_EQ(vaultPath.generic_string(), "/folder/");
    EXPECT_EQ(ObjectStore::vaultPathToKey(vaultPath), "folder/");
}

TEST(S3GatewayRouterTest, ParsesPathStyleBucketAndPreservesPlusInKey) {
    using vh::protocols::s3::Router;

    Router::Request request{boost::beast::http::verb::get, "/bucket/a+b%2Bc.txt?prefix=a+b&encoding-type=url", 11};
    request.set(boost::beast::http::field::host, "127.0.0.1:39000");

    const auto parsed = Router::parseRequestTarget(request);
    EXPECT_EQ(parsed.bucket, "bucket");
    EXPECT_EQ(parsed.key, "a+b+c.txt");
    ASSERT_TRUE(parsed.query.contains("prefix"));
    EXPECT_EQ(parsed.query.at("prefix"), "a+b");
    EXPECT_EQ(parsed.query.at("encoding-type"), "url");
}

TEST(S3GatewayRouterTest, ParsesVirtualHostedBucket) {
    using vh::protocols::s3::Router;

    Router::Request request{boost::beast::http::verb::get, "/folder/a%20b+plus.txt?uploads", 11};
    request.set(boost::beast::http::field::host, "photos.example.test:39000");

    const auto parsed = Router::parseRequestTarget(request);
    EXPECT_EQ(parsed.bucket, "photos");
    EXPECT_EQ(parsed.key, "folder/a b+plus.txt");
    EXPECT_TRUE(parsed.query.contains("uploads"));
}

TEST(S3GatewayRouterTest, RejectsPathStyleWhenDisabled) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.allow_path_style = false;
    cfg.s3_gateway.allow_virtual_hosted_style = true;
    vh::config::Registry::set(cfg);

    using vh::protocols::s3::Router;
    Router::Request request{boost::beast::http::verb::get, "/bucket/object.txt", 11};
    request.set(boost::beast::http::field::host, "127.0.0.1:39000");

    EXPECT_THROW((void)Router::parseRequestTarget(request), vh::protocols::s3::S3Error);
}

TEST(S3GatewayRouterTest, HonorsVirtualHostedStyleDisabled) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.allow_path_style = true;
    cfg.s3_gateway.allow_virtual_hosted_style = false;
    vh::config::Registry::set(cfg);

    using vh::protocols::s3::Router;
    Router::Request request{boost::beast::http::verb::get, "/path-bucket/object.txt", 11};
    request.set(boost::beast::http::field::host, "photos.example.test:39000");

    const auto parsed = Router::parseRequestTarget(request);
    EXPECT_EQ(parsed.bucket, "path-bucket");
    EXPECT_EQ(parsed.key, "object.txt");
}

TEST(S3GatewayRouterTest, ParsesCopySourceBeforeDecodingQuerySeparators) {
    using vh::protocols::s3::Router;

    const auto source = Router::parseCopySource("/source-bucket/folder/a%3Fb%2Bc.txt?versionId=123");
    EXPECT_EQ(source.bucket, "source-bucket");
    EXPECT_EQ(source.key, "folder/a?b+c.txt");
    ASSERT_TRUE(source.query.contains("versionId"));
    EXPECT_EQ(source.query.at("versionId"), "123");
}

TEST(S3GatewayRouterTest, ComputesAwsChecksumHeaderValues) {
    using vh::protocols::s3::Router;

    const std::vector<uint8_t> hello{'h', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(Router::checksumCrc32Base64(hello), "NhCmhg==");
    EXPECT_EQ(Router::checksumSha256Base64(hello), "LPJNul+wow4m6DsqxbninhsWHlwfp0JecwQzYpOLmCQ=");
}

TEST(S3GatewaySigV4Test, CanonicalizesUriAndQuery) {
    using namespace vh::protocols::s3::sigv4;

    EXPECT_EQ(canonicalUri("/photos/a b/%7Efile.txt"), "/photos/a%20b/~file.txt");
    EXPECT_EQ(canonicalUri("/photos/a+b.txt"), "/photos/a%2Bb.txt");
    EXPECT_EQ(canonicalQueryString("b=two&a=1&X-Amz-Signature=dead&a=0&space=a%20b"),
              "a=0&a=1&b=two&space=a%20b");
    EXPECT_EQ(canonicalQueryString("prefix=a+b&space=a%20b"), "prefix=a%2Bb&space=a%20b");
}

TEST(S3GatewaySigV4Test, VerifiesHeaderSignatureAndRejectsTampering) {
    using namespace vh::protocols::s3::sigv4;

    const std::string secret = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    const std::string accessKey = "VHTESTACCESSKEY";
    const std::string amzDate = amzNow();

    VerificationInput input{
        .method = "PUT",
        .target = "/bucket/path/to/object.txt?partNumber=1&uploadId=upload",
        .host = "localhost:39000",
        .headers = {
            {"host", "localhost:39000"},
            {"x-amz-content-sha256", sha256Hex("payload")},
            {"x-amz-date", amzDate}
        },
        .body = "payload",
        .body_sha256 = std::nullopt
    };

    ParsedAuth auth{
        .credential = {
            .access_key = accessKey,
            .date = scopeDate(amzDate),
            .region = "us-east-1",
            .service = "s3"
        },
        .signed_headers = "host;x-amz-content-sha256;x-amz-date",
        .signature = {},
        .amz_date = amzDate,
        .payload_hash = sha256Hex("payload")
    };

    auth.signature = signatureFor(input, auth, secret);
    input.headers["authorization"] =
        "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + auth.credential.date + "/us-east-1/s3/aws4_request, "
        "SignedHeaders=" + auth.signed_headers + ", Signature=" + auth.signature;

    auto ok = verify(input, secret);
    EXPECT_TRUE(ok.ok) << ok.error;
    EXPECT_EQ(ok.access_key, accessKey);

    input.body.clear();
    input.body_sha256 = sha256Hex("payload");
    auto streamedOk = verify(input, secret);
    EXPECT_TRUE(streamedOk.ok) << streamedOk.error;

    input.body_sha256 = sha256Hex("tampered");
    auto bad = verify(input, secret);
    EXPECT_FALSE(bad.ok);
}

TEST(S3GatewaySigV4Test, AcceptsAwsStreamingUnsignedPayloadTrailerHashConstant) {
    using namespace vh::protocols::s3::sigv4;

    const std::string secret = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    const std::string accessKey = "VHTESTACCESSKEY";
    const std::string amzDate = amzNow();
    constexpr std::string_view payloadMode = "STREAMING-UNSIGNED-PAYLOAD-TRAILER";

    VerificationInput input{
        .method = "PUT",
        .target = "/bucket/object.txt",
        .host = "localhost:39000",
        .headers = {
            {"host", "localhost:39000"},
            {"x-amz-content-sha256", std::string(payloadMode)},
            {"x-amz-date", amzDate},
            {"x-amz-decoded-content-length", "5"},
            {"x-amz-trailer", "x-amz-checksum-crc32"}
        },
        .body = "aws-chunked-wire-body",
        .body_sha256 = sha256Hex("decoded")
    };

    ParsedAuth auth{
        .credential = {
            .access_key = accessKey,
            .date = scopeDate(amzDate),
            .region = "us-east-1",
            .service = "s3"
        },
        .signed_headers = "host;x-amz-content-sha256;x-amz-date;x-amz-decoded-content-length;x-amz-trailer",
        .signature = {},
        .amz_date = amzDate,
        .payload_hash = std::string(payloadMode)
    };
    auth.signature = signatureFor(input, auth, secret);
    input.headers["authorization"] =
        "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + auth.credential.date + "/us-east-1/s3/aws4_request, "
        "SignedHeaders=" + auth.signed_headers + ", Signature=" + auth.signature;

    const auto ok = verify(input, secret);
    EXPECT_TRUE(ok.ok) << ok.error;
    EXPECT_EQ(ok.access_key, accessKey);
}

TEST(S3GatewaySigV4Test, RejectsMissingSignedHeader) {
    using namespace vh::protocols::s3::sigv4;

    const std::string secret = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    const std::string accessKey = "VHTESTACCESSKEY";
    const std::string amzDate = amzNow();

    VerificationInput input{
        .method = "GET",
        .target = "/bucket/object.txt",
        .host = "localhost:39000",
        .headers = {
            {"host", "localhost:39000"},
            {"x-amz-content-sha256", "UNSIGNED-PAYLOAD"},
            {"x-amz-date", amzDate}
        },
        .body = {},
        .body_sha256 = std::nullopt
    };

    ParsedAuth auth{
        .credential = {
            .access_key = accessKey,
            .date = scopeDate(amzDate),
            .region = "us-east-1",
            .service = "s3"
        },
        .signed_headers = "host;x-amz-content-sha256;x-amz-date;x-amz-meta-missing",
        .signature = {},
        .amz_date = amzDate,
        .payload_hash = "UNSIGNED-PAYLOAD"
    };
    auth.signature = signatureFor(input, auth, secret);
    input.headers["authorization"] =
        "AWS4-HMAC-SHA256 Credential=" + accessKey + "/" + auth.credential.date + "/us-east-1/s3/aws4_request, "
        "SignedHeaders=" + auth.signed_headers + ", Signature=" + auth.signature;

    const auto result = verify(input, secret);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("Signed header missing"), std::string::npos);
}

TEST(S3GatewaySigV4Test, RejectsMalformedPresignedExpiresWithoutThrowing) {
    using namespace vh::protocols::s3::sigv4;

    VerificationInput input{
        .method = "GET",
        .target = "/bucket/object.txt"
                  "?X-Amz-Algorithm=AWS4-HMAC-SHA256"
                  "&X-Amz-Credential=VHTESTACCESSKEY%2F20260531%2Fus-east-1%2Fs3%2Faws4_request"
                  "&X-Amz-Date=20260531T120000Z"
                  "&X-Amz-Expires=not-a-number"
                  "&X-Amz-SignedHeaders=host"
                  "&X-Amz-Signature=deadbeef",
        .host = "localhost:39000",
        .headers = {{"host", "localhost:39000"}},
        .body = {},
        .body_sha256 = std::nullopt
    };

    EXPECT_NO_THROW({
        const auto result = verify(input, "secret");
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error, "Invalid X-Amz-Expires");
    });
}

TEST(S3GatewaySigV4Test, RejectsPresignedUrlsDatedTooFarInFuture) {
    using namespace vh::protocols::s3::sigv4;

    const std::string amzDate = amzAfter(std::chrono::hours(1));
    VerificationInput input{
        .method = "GET",
        .target = "/bucket/object.txt"
                  "?X-Amz-Algorithm=AWS4-HMAC-SHA256"
                  "&X-Amz-Credential=VHTESTACCESSKEY%2F" + scopeDate(amzDate) + "%2Fus-east-1%2Fs3%2Faws4_request"
                  "&X-Amz-Date=" + amzDate +
                  "&X-Amz-Expires=60"
                  "&X-Amz-SignedHeaders=host"
                  "&X-Amz-Signature=deadbeef",
        .host = "localhost:39000",
        .headers = {{"host", "localhost:39000"}},
        .body = {},
        .body_sha256 = std::nullopt
    };

    const auto result = verify(input, "secret");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "Signature date is outside allowed skew");
}

TEST(S3GatewayServiceTest, EnabledServiceBindsAndReturnsS3XmlErrors) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.enabled = true;
    cfg.s3_gateway.host = "127.0.0.1";
    cfg.s3_gateway.port = freeLoopbackPort();
    cfg.s3_gateway.require_sigv4 = true;
    vh::config::Registry::set(cfg);

    vh::concurrency::ThreadPoolManager::instance().init();
    ThreadPoolShutdown shutdownPools{true};

    vh::protocols::s3::GatewayService service;
    service.start();

    auto status = service.gatewayStatus();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!status.ready && service.isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        status = service.gatewayStatus();
    }

    ASSERT_TRUE(status.configured);
    ASSERT_TRUE(status.ready);

    const auto response = httpGetRoot(cfg.s3_gateway.port);

    EXPECT_EQ(response.result(), http::status::forbidden);
    EXPECT_NE(response.body().find("<Code>SignatureDoesNotMatch</Code>"), std::string::npos);
    EXPECT_NE(response.body().find("<RequestId>"), std::string::npos);
    EXPECT_FALSE(response["x-amz-request-id"].empty());
    EXPECT_GE(service.gatewayStatus().totalRequests, 1u);

    service.stop();
}

TEST(S3GatewayServiceTest, DisabledServiceReportsNotConfiguredWithoutFailing) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.enabled = false;
    cfg.s3_gateway.host = "127.0.0.1";
    cfg.s3_gateway.port = freeLoopbackPort();
    vh::config::Registry::set(cfg);

    vh::protocols::s3::GatewayService service;
    service.start();

    auto status = service.gatewayStatus();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (status.host.empty() && service.isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        status = service.gatewayStatus();
    }

    EXPECT_TRUE(status.running);
    EXPECT_FALSE(status.configured);
    EXPECT_FALSE(status.ready);
    EXPECT_EQ(status.host, "127.0.0.1");
    EXPECT_EQ(status.port, cfg.s3_gateway.port);

    service.stop();
}

class S3GatewayDbTest : public ::testing::Test {
protected:
    inline static bool skipTests = false;
    inline static uint32_t userId = 0;
    inline static uint32_t vaultId = 0;

    static bool hasDbEnv() {
        return std::getenv("VH_TEST_DB_USER") &&
               std::getenv("VH_TEST_DB_PASS") &&
               std::getenv("VH_TEST_DB_HOST") &&
               std::getenv("VH_TEST_DB_PORT") &&
               std::getenv("VH_TEST_DB_NAME");
    }

    static void SetUpTestSuite() {
        if (!hasDbEnv()) {
            skipTests = true;
            std::cout << "[test_s3_gateway] Skipping db tests due to missing environment variables." << std::endl;
            return;
        }

        vh::paths::enableTestMode();
        vh::db::Transactions::init();
        vh::db::seed::nuke_and_recreate_schema_public();
        vh::db::Transactions::dbPool_->initPreparedStatements();

        vaultId = vh::db::Transactions::exec("S3GatewayDbTest::seed", [](pqxx::work& txn) {
            S3GatewayDbTest::userId = txn.exec(
                "INSERT INTO users (name, email, password_hash) VALUES ($1, $2, $3) RETURNING id",
                pqxx::params{"s3_gateway_user", "s3-gateway@vaulthalla.test", "hash"}
            ).one_field().as<uint32_t>();

            return txn.exec(
                "INSERT INTO vault (type, name, owner_id, mount_point) VALUES ($1, $2, $3, $4) RETURNING id",
                pqxx::params{"local", "S3 Gateway Test Vault", userId, "s3_gateway_test"}
            ).one_field().as<uint32_t>();
        });
    }

    void SetUp() override {
        if (skipTests) GTEST_SKIP() << "Skipping db tests due to missing environment variables.";
        vh::db::Transactions::exec("S3GatewayDbTest::clearObjects", [](pqxx::work& txn) {
            txn.exec("DELETE FROM s3_gateway_multipart_upload");
            txn.exec("DELETE FROM s3_gateway_object");
            txn.exec("DELETE FROM remote_object_index");
        });
    }

    static void putObject(const std::string& key) {
        vh::db::query::s3::Gateway::upsertObject({
            .vault_id = vaultId,
            .object_key = key,
            .etag = "\"etag-" + key + "\"",
            .size_bytes = 1,
            .content_type = "text/plain",
            .storage_class = std::nullopt,
            .last_modified = std::time(nullptr),
            .multipart = false,
            .part_count = std::nullopt
        });
    }
};

TEST_F(S3GatewayDbTest, ListObjectsContinuationTokenDoesNotSkipFirstObjectOnNextPage) {
    putObject("a.txt");
    putObject("b.txt");
    putObject("c.txt");

    vh::db::query::s3::ObjectListParams firstParams;
    firstParams.max_keys = 2;
    const auto first = vh::db::query::s3::Gateway::listObjectStates(vaultId, firstParams);

    ASSERT_TRUE(first.is_truncated);
    ASSERT_EQ(first.objects.size(), 2u);
    ASSERT_TRUE(first.next_continuation_token);
    EXPECT_EQ(*first.next_continuation_token, "b.txt");

    vh::db::query::s3::ObjectListParams secondParams;
    secondParams.max_keys = 2;
    secondParams.continuation_token = first.next_continuation_token;
    const auto second = vh::db::query::s3::Gateway::listObjectStates(vaultId, secondParams);

    ASSERT_FALSE(second.is_truncated);
    ASSERT_EQ(second.objects.size(), 1u);
    EXPECT_EQ(second.objects[0].object_key, "c.txt");
}

TEST_F(S3GatewayDbTest, ListObjectsMaxKeysZeroReturnsNoObjects) {
    putObject("a.txt");

    vh::db::query::s3::ObjectListParams params;
    params.max_keys = 0;
    const auto result = vh::db::query::s3::Gateway::listObjectStates(vaultId, params);

    EXPECT_FALSE(result.is_truncated);
    EXPECT_FALSE(result.next_continuation_token);
    EXPECT_TRUE(result.objects.empty());
    EXPECT_TRUE(result.common_prefixes.empty());
}

TEST_F(S3GatewayDbTest, ListObjectsTreatsPrefixWildcardCharactersLiterally) {
    putObject("a%literal.txt");
    putObject("a_literal.txt");
    putObject("ab.txt");

    vh::db::query::s3::ObjectListParams percentParams;
    percentParams.prefix = "a%";
    const auto percent = vh::db::query::s3::Gateway::listObjectStates(vaultId, percentParams);

    ASSERT_EQ(percent.objects.size(), 1u);
    EXPECT_EQ(percent.objects[0].object_key, "a%literal.txt");

    vh::db::query::s3::ObjectListParams underscoreParams;
    underscoreParams.prefix = "a_";
    const auto underscore = vh::db::query::s3::Gateway::listObjectStates(vaultId, underscoreParams);

    ASSERT_EQ(underscore.objects.size(), 1u);
    EXPECT_EQ(underscore.objects[0].object_key, "a_literal.txt");
}

TEST_F(S3GatewayDbTest, AbortExpiredMultipartUploadsUsesConfiguredRetention) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    const auto partDir = std::filesystem::temp_directory_path() /
                         ("vh_s3_gateway_expired_parts_" + std::to_string(vaultId));
    std::filesystem::remove_all(partDir);

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.multipart.part_dir = partDir;
    cfg.s3_gateway.multipart.abort_after_days = 1;
    vh::config::Registry::set(cfg);

    const std::string oldUpload = "old-upload-" + std::to_string(vaultId);
    const std::string freshUpload = "fresh-upload-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::createMultipartUpload({
        .upload_id = oldUpload,
        .vault_id = vaultId,
        .object_key = "old.txt",
        .initiated_by = userId,
        .content_type = std::nullopt,
        .metadata = {},
        .storage_class = std::nullopt
    });
    vh::db::query::s3::Gateway::createMultipartUpload({
        .upload_id = freshUpload,
        .vault_id = vaultId,
        .object_key = "fresh.txt",
        .initiated_by = userId,
        .content_type = std::nullopt,
        .metadata = {},
        .storage_class = std::nullopt
    });

    const auto oldPartPath = vh::protocols::s3::MultipartStore::partRoot() / oldUpload / "1";
    std::filesystem::create_directories(oldPartPath.parent_path());
    std::ofstream(oldPartPath, std::ios::binary) << "old part";
    vh::db::query::s3::Gateway::upsertMultipartPart({
        .upload_id = oldUpload,
        .part_number = 1,
        .etag = "\"etag\"",
        .size_bytes = 8,
        .md5 = std::vector<uint8_t>(16, 0),
        .path = oldPartPath,
        .created_at = std::time(nullptr)
    });

    const auto cutoffTime = std::time(nullptr) - 2 * 24 * 60 * 60;
    vh::db::Transactions::exec("S3GatewayDbTest::ageMultipartUpload", [&](pqxx::work& txn) {
        txn.exec(
            "UPDATE s3_gateway_multipart_upload SET initiated_at = TO_TIMESTAMP($1::double precision) WHERE upload_id = $2",
            pqxx::params{cutoffTime, oldUpload});
    });

    vh::protocols::s3::MultipartStore store;
    EXPECT_EQ(store.abortExpiredUploads(), 1u);

    EXPECT_FALSE(vh::db::query::s3::Gateway::getMultipartUpload(oldUpload));
    EXPECT_TRUE(vh::db::query::s3::Gateway::listMultipartParts(oldUpload).empty());
    EXPECT_FALSE(std::filesystem::exists(oldPartPath));
    EXPECT_TRUE(vh::db::query::s3::Gateway::getMultipartUpload(freshUpload));

    std::filesystem::remove_all(partDir);
}

TEST_F(S3GatewayDbTest, DeleteObjectStateAndRemoteIndexRemovesGatewayAndRemoteRows) {
    putObject("delete-me.txt");
    vh::db::query::s3::Gateway::upsertObjectMetadata(
        vaultId,
        "delete-me.txt",
        {{"color", "blue"}});

    vh::db::Transactions::exec("S3GatewayDbTest::insertRemoteObjectIndex", [](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO remote_object_index (vault_id, object_key, size_bytes, etag, source)
                VALUES ($1, $2, $3, $4, $5)
            )SQL",
            pqxx::params{S3GatewayDbTest::vaultId, "delete-me.txt", 1, "\"remote-etag\"", "event"});
    });

    vh::db::query::s3::Gateway::deleteObjectStateAndRemoteIndex(vaultId, "/delete-me.txt");

    EXPECT_FALSE(vh::db::query::s3::Gateway::getObjectState(vaultId, "delete-me.txt"));
    EXPECT_TRUE(vh::db::query::s3::Gateway::listObjectMetadata(vaultId, "delete-me.txt").empty());

    const auto remoteRows = vh::db::Transactions::exec(
        "S3GatewayDbTest::countRemoteObjectIndex",
        [](pqxx::work& txn) {
            return txn.exec(
                "SELECT COUNT(*) FROM remote_object_index WHERE vault_id = $1 AND object_key = $2",
                pqxx::params{S3GatewayDbTest::vaultId, "delete-me.txt"}).one_field().as<int>();
        });
    EXPECT_EQ(remoteRows, 0);
}

TEST_F(S3GatewayDbTest, RemoteBackedListDetectsStaleRemoteIndex) {
    vh::db::Transactions::exec("S3GatewayDbTest::insertStaleRemoteObjectIndex", [](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO remote_object_index (vault_id, object_key, size_bytes, etag, source, indexed_at)
                VALUES ($1, $2, $3, $4, $5, TO_TIMESTAMP($6::double precision))
            )SQL",
            pqxx::params{
                S3GatewayDbTest::vaultId,
                "stale-index.txt",
                1,
                "\"remote-etag\"",
                "manifest",
                std::time(nullptr) - 2 * 24 * 60 * 60});
    });

    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->id = vaultId;
    vault->name = "S3 Gateway Test Vault";
    vault->mount_point = "s3_gateway_test";

    auto policy = std::make_shared<vh::sync::model::RemotePolicy>();
    policy->max_remote_index_age = std::chrono::seconds(60);

    auto engine = std::make_shared<vh::storage::CloudEngine>();
    engine->vault = vault;
    engine->sync = policy;

    const vh::protocols::s3::ResolvedBucket bucket{
        .bucket_name = "remote-cache",
        .vault_id = vaultId,
        .mode = "remote_cache",
        .api_exclusive = true,
        .engine = engine,
        .actor = vh::db::query::identities::User::getUserById(userId)
    };

    const vh::protocols::s3::ObjectStore store;
    EXPECT_TRUE(store.remoteIndexStale(bucket));

    policy->max_remote_index_age = std::chrono::hours(24 * 7);
    EXPECT_FALSE(store.remoteIndexStale(bucket));
}

TEST_F(S3GatewayDbTest, GatewayServiceBindsAndServesS3XmlWhenEnabled) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.enabled = true;
    cfg.s3_gateway.host = "127.0.0.1";
    cfg.s3_gateway.port = freeLoopbackPort();
    cfg.s3_gateway.require_sigv4 = false;
    vh::config::Registry::set(cfg);

    vh::concurrency::ThreadPoolManager::instance().init();
    ThreadPoolShutdown shutdownPools{true};

    vh::protocols::s3::GatewayService service;
    service.start();

    auto status = service.gatewayStatus();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!status.ready && service.isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        status = service.gatewayStatus();
    }

    ASSERT_TRUE(status.configured);
    ASSERT_TRUE(status.ready);

    const auto response = httpGetRoot(cfg.s3_gateway.port);

    EXPECT_EQ(response.result(), http::status::ok);
    EXPECT_NE(response.body().find("<ListAllMyBucketsResult"), std::string::npos);
    EXPECT_FALSE(response["x-amz-request-id"].empty());
    EXPECT_GE(service.gatewayStatus().totalRequests, 1u);

    service.stop();
}

TEST_F(S3GatewayDbTest, ListObjectsDelimiterPaginatesCommonPrefixesOnce) {
    putObject("a/1.txt");
    putObject("a/2.txt");
    putObject("b/1.txt");

    vh::db::query::s3::ObjectListParams firstParams;
    firstParams.max_keys = 1;
    firstParams.delimiter = "/";
    const auto first = vh::db::query::s3::Gateway::listObjectStates(vaultId, firstParams);

    ASSERT_TRUE(first.is_truncated);
    ASSERT_TRUE(first.objects.empty());
    ASSERT_EQ(first.common_prefixes.size(), 1u);
    EXPECT_EQ(first.common_prefixes[0], "a/");
    ASSERT_TRUE(first.next_continuation_token);
    EXPECT_EQ(*first.next_continuation_token, "a/");

    vh::db::query::s3::ObjectListParams secondParams;
    secondParams.max_keys = 1;
    secondParams.delimiter = "/";
    secondParams.continuation_token = first.next_continuation_token;
    const auto second = vh::db::query::s3::Gateway::listObjectStates(vaultId, secondParams);

    ASSERT_FALSE(second.is_truncated);
    ASSERT_TRUE(second.objects.empty());
    ASSERT_EQ(second.common_prefixes.size(), 1u);
    EXPECT_EQ(second.common_prefixes[0], "b/");
}
