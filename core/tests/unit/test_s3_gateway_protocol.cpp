// S3 gateway, part 1/3 (no database): config parsing, multipart part root, XML, object-store keys/ETags,
// pricing operation names, request routing, SigV4 verification and the gateway service lifecycle.

#include "support/s3_gateway_fixture.hpp"

namespace vh::test::s3_gateway {

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
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.list, "0.00000001");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.head, "0.00000001");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.get, "0.00000001");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.put, "0.00000001");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.delete_, "0.00000001");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.copy, "0.00000001");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.downloaded_gb, "0.00000000");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.uploaded_gb, "0.00000000");
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
synthetic_local_request_cost_usd:
  list: "0.00000002"
  head: "0.00000003"
  get: "0.00000004"
  put: "0.00000005"
  delete: "0.00000006"
  copy: "0.00000007"
  downloaded_gb: "0.00000008"
  uploaded_gb: "0.00000009"
)yaml");

    const auto cfg = node.as<vh::config::S3GatewayConfig>();

    EXPECT_TRUE(cfg.enabled);
    EXPECT_EQ(cfg.host, "127.0.0.1");
    EXPECT_EQ(cfg.port, 39123);
    EXPECT_EQ(cfg.max_body_size_bytes, 64ull * 1024ull * 1024ull);
    EXPECT_FALSE(cfg.require_sigv4);
    EXPECT_FALSE(cfg.allow_path_style);
    EXPECT_TRUE(cfg.allow_virtual_hosted_style);
    EXPECT_EQ(cfg.multipart.min_part_size_mb, 5u);
    EXPECT_EQ(cfg.multipart.abort_after_days, 1u);
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.list, "0.00000002");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.head, "0.00000003");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.get, "0.00000004");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.put, "0.00000005");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.delete_, "0.00000006");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.copy, "0.00000007");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.downloaded_gb, "0.00000008");
    EXPECT_EQ(cfg.synthetic_local_request_cost_usd.uploaded_gb, "0.00000009");
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

TEST(S3GatewayConfigTest, MultipartPartDirIsNotEmittedAndLegacyPartDirIsIgnored) {
    const auto yamlCfg = YAML::Load(R"yaml(
multipart:
  part_dir: /tmp/legacy-vh-s3-parts
  min_part_size_mb: 8
  abort_after_days: 3
)yaml").as<vh::config::S3GatewayConfig>();
    EXPECT_EQ(yamlCfg.multipart.min_part_size_mb, 8u);
    EXPECT_EQ(yamlCfg.multipart.abort_after_days, 3u);

    const auto yamlNode = YAML::convert<vh::config::S3GatewayMultipartConfig>::encode(yamlCfg.multipart);
    EXPECT_FALSE(static_cast<bool>(yamlNode["part_dir"]));

    nlohmann::json jsonCfg = yamlCfg.multipart;
    EXPECT_FALSE(jsonCfg.contains("part_dir"));

    const auto fromJson = nlohmann::json{
        {"part_dir", "/tmp/legacy-json-vh-s3-parts"},
        {"min_part_size_mb", 9},
        {"abort_after_days", 4}
    }.get<vh::config::S3GatewayMultipartConfig>();
    EXPECT_EQ(fromJson.min_part_size_mb, 9u);
    EXPECT_EQ(fromJson.abort_after_days, 4u);
}

TEST(S3GatewayMultipartTest, PartRootUsesGeneratedHiddenBackingPath) {
    const auto oldBackingPath = vh::paths::backingPath;
    const auto tempBacking = std::filesystem::temp_directory_path() / s3GatewayUniqueSuffix("vh_s3_gateway_parts_root");
    vh::paths::backingPath = tempBacking;

    EXPECT_EQ(vh::protocols::s3::MultipartStore::partRoot(),
              vh::paths::getS3GatewayMultipartPartsPath());
    EXPECT_EQ(vh::protocols::s3::MultipartStore::partRoot().filename().string(),
              std::string(VH_S3_GATEWAY_MULTIPART_PARTS_DIRNAME));

    vh::paths::backingPath = tempBacking / "changed";
    EXPECT_EQ(vh::protocols::s3::MultipartStore::partRoot(),
              vh::paths::getS3GatewayMultipartPartsPath());

    vh::paths::backingPath = oldBackingPath;
    std::filesystem::remove_all(tempBacking);
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

TEST(S3GatewayObjectStoreTest, OnlyDevCredentialFastPathBypassesDatabase) {
    using vh::protocols::s3::AuthContext;
    using vh::protocols::s3::ObjectStore;
    using Action = vh::rbac::permission::vault::FilesystemAction;

    AuthContext dev;
    dev.credential_id = 123;
    dev.scope_mode = "vault_allowlist";
    dev.dev_context = true;
    EXPECT_TRUE(ObjectStore::credentialAllows(dev, 55, Action::Delete));

    AuthContext userAccess;
    userAccess.credential_id = 123;
    userAccess.scope_mode = "user_access";
    EXPECT_FALSE(ObjectStore::credentialAllows(userAccess, 55, Action::Read));

    AuthContext unknownScope;
    unknownScope.credential_id = 123;
    unknownScope.scope_mode = "unknown";
    EXPECT_FALSE(ObjectStore::credentialAllows(unknownScope, 55, Action::Read));
}

TEST(S3GatewayObjectStoreTest, UserAccessCredentialDoesNotAuthorizeBucketAdminWithoutGatewayRole) {
    using vh::protocols::s3::GatewayAccessContext;
    using vh::protocols::s3::ObjectStore;
    using vh::protocols::s3::ResolvedBucket;

    auto user = std::make_shared<vh::identities::User>();
    user->id = 42;
    user->name = "gateway-user-access-non-admin";
    user->roles.admin = std::make_shared<vh::rbac::role::Admin>(
        vh::rbac::role::Admin::None(user->id));

    ResolvedBucket bucket{
        .bucket_name = "admin-op",
        .vault_id = 55,
        .mode = "local",
        .api_exclusive = true,
        .engine = nullptr,
        .actor = user,
        .gateway_access = GatewayAccessContext{
            .credential_id = 123,
            .access_key = "VHTESTUSERACCESS",
            .scope_mode = "user_access",
            .credential = {},
            .dev_context = false
        }
    };

    EXPECT_FALSE(ObjectStore::credentialAllowsAdmin(bucket));

    user->roles.admin = std::make_shared<vh::rbac::role::Admin>(
        vh::rbac::role::Admin::SuperAdmin(user->id));
    EXPECT_FALSE(ObjectStore::credentialAllowsAdmin(bucket));
}

TEST(S3GatewayPricingTest, OperationNamesMatchBudgetLedgerValues) {
    using namespace vh::storage::s3::pricing;

    EXPECT_EQ(toString(S3GatewayOperation::PutObject), "PutObject");
    EXPECT_EQ(toString(S3GatewayOperation::GetObject), "GetObject");
    EXPECT_EQ(toString(S3GatewayOperation::DeleteObject), "DeleteObject");
    EXPECT_EQ(toString(S3GatewayOperation::CompleteMultipartUpload), "CompleteMultipartUpload");
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

TEST(S3GatewayRouterTest, DedicatedS3HostUsesPathStyleWhenProxyMarkerIsPresent) {
    using vh::protocols::s3::Router;

    Router::Request object{boost::beast::http::verb::get, "/bucket/a%2Fb.txt?uploadId=1", 11};
    object.set(boost::beast::http::field::host, "s3.vaulthalla.dev");
    object.set("X-Vaulthalla-S3-Path-Style-Only", "true");
    const auto parsed = Router::parseRequestTarget(object);
    EXPECT_EQ(parsed.bucket, "bucket");
    EXPECT_EQ(parsed.key, "a/b.txt");
    ASSERT_TRUE(parsed.query.contains("uploadId"));
    EXPECT_EQ(parsed.query.at("uploadId"), "1");
}

TEST(S3GatewayRouterTest, ApiS3IsOrdinaryPathStyleWithoutPrefixRouting) {
    using vh::protocols::s3::Router;

    Router::Request request{boost::beast::http::verb::get, "/api/s3/bucket/key.txt", 11};
    request.set(boost::beast::http::field::host, "127.0.0.1:39000");

    const auto parsed = Router::parseRequestTarget(request);
    EXPECT_EQ(parsed.bucket, "api");
    EXPECT_EQ(parsed.key, "s3/bucket/key.txt");
}

TEST(S3GatewayRouterTest, PublicHostStillTriggersVirtualHostedBucketWithoutProxyMarker) {
    using vh::protocols::s3::Router;

    Router::Request request{boost::beast::http::verb::get, "/path-bucket/object.txt", 11};
    request.set(boost::beast::http::field::host, "photos.example.test:39000");

    const auto parsed = Router::parseRequestTarget(request);
    EXPECT_EQ(parsed.bucket, "photos");
    EXPECT_EQ(parsed.key, "path-bucket/object.txt");
}

TEST(S3GatewayRouterTest, FalsePathStyleOnlyMarkerLeavesVirtualHostedBucketEnabled) {
    using vh::protocols::s3::Router;

    Router::Request request{boost::beast::http::verb::get, "/path-bucket/object.txt", 11};
    request.set(boost::beast::http::field::host, "photos.example.test:39000");
    request.set("X-Vaulthalla-S3-Path-Style-Only", "false");

    const auto parsed = Router::parseRequestTarget(request);
    EXPECT_EQ(parsed.bucket, "photos");
    EXPECT_EQ(parsed.key, "path-bucket/object.txt");
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

// Sessions run on pool threads with sockets bound to the gateway's io_context: stopping must not free the context
// while one is still alive (heap-use-after-free under ASan), and must end idle keep-alive connections.
TEST(S3GatewayServiceTest, StopEndsOpenConnectionsBeforeFreeingTheirContext) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.enabled = true;
    cfg.s3_gateway.host = "127.0.0.1";
    cfg.s3_gateway.port = freeLoopbackPort();
    vh::config::Registry::set(cfg);

    vh::concurrency::ThreadPoolManager::instance().init();
    ThreadPoolShutdown shutdownPools{true};

    vh::protocols::s3::GatewayService service;
    service.start();

    const auto readyBy = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!service.gatewayStatus().ready && std::chrono::steady_clock::now() < readyBy)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    ASSERT_TRUE(service.gatewayStatus().ready);

    boost::asio::io_context clientContext;
    std::vector<boost::asio::ip::tcp::socket> idle;
    for (int i = 0; i < 3; ++i) {
        idle.emplace_back(clientContext);
        idle.back().connect({boost::asio::ip::make_address("127.0.0.1"), cfg.s3_gateway.port});
    }

    const auto acceptedBy = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (vh::protocols::s3::Session::metrics().activeSessions < idle.size() &&
           std::chrono::steady_clock::now() < acceptedBy)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_GE(vh::protocols::s3::Session::metrics().activeSessions, idle.size());

    const auto stopStarted = std::chrono::steady_clock::now();
    service.stop();

    EXPECT_EQ(vh::protocols::s3::Session::metrics().activeSessions, 0u);
    EXPECT_LT(std::chrono::steady_clock::now() - stopStarted, std::chrono::seconds(5));
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

} // namespace vh::test::s3_gateway
