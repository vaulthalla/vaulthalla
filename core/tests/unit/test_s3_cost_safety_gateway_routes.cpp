// S3CostSafetyTest, part 3/4: signed S3 gateway routes against credential/vault budgets (remote and
// local-first PUT/GET/LIST/DELETE/multipart/copy, request and byte budgets, synthetic local budgets).

#include "support/s3_cost_safety_fixture.hpp"

namespace vh::test::s3_cost_safety {

TEST(S3CostSafetyTest, GatewayRemotePutLocalFirstDoesNotCommitCredentialBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-put-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");

    auto request = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/put-object.txt",
        fixture.secret,
        "payload");

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    EXPECT_TRUE(vh::db::query::fs::File::getFileByPath(fixture.vault_id, "/put-object.txt"));
    ASSERT_TRUE(vh::db::query::s3::Gateway::getObjectState(fixture.vault_id, "put-object.txt"));
    EXPECT_EQ(1u, countGatewaySyncOriginForDbTest(fixture.vault_id, "put-object.txt", "put"));
    expectGatewayLedgerAbsent(fixture, "PutObject", "put-object.txt");
}

TEST(S3CostSafetyTest, GatewayRemoteGetRoutePreflightsAndCommitsCredentialVaultBudget) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-get-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        "1.00000000");
    constexpr std::string_view objectKey = "remote-get.txt";
    fixture.controller->download_payload = {'r', 'e', 'm', 'o', 't', 'e'};
    fixture.controller->head_response = std::unordered_map<std::string, std::string>{
        {"x-amz-meta-vh-encrypted", "false"}
    };
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"remote-get-etag\"",
        .size_bytes = fixture.controller->download_payload.size(),
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    auto request = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ("remote", response.body());
    EXPECT_EQ(1, fixture.controller->download_to_buffer_calls);
    expectGatewayLedgerCommitted(fixture, "GetObject", std::string(objectKey), false, "remote_download");
}

TEST(S3CostSafetyTest, GatewayRemoteGetPriceBudgetDeniedReturnsXmlAccessDeniedBeforeRemoteDownload) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-get-price-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "0.00000000");
    constexpr std::string_view objectKey = "price-budget-get.txt";
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"price-budget-get\"",
        .size_bytes = 128,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    auto request = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::forbidden);
    EXPECT_NE(std::string::npos, response.body().find("<Code>AccessDenied</Code>"));
    EXPECT_NE(std::string::npos, response.body().find("S3 gateway price budget exceeded"));
    EXPECT_NE(std::string::npos, response.body().find("operation=GetObject"));
    EXPECT_NE(std::string::npos, response.body().find("provider_key=aws-s3"));
    EXPECT_EQ(0, fixture.controller->download_to_buffer_calls);
    expectGatewayLedgerAbsent(fixture, "GetObject", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayRemoteListDoesNotConsumeRequestOrPriceBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-list-no-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "0.00000100");
    setGatewayRouteRequestBudget(fixture, [](auto& budget) {
        budget.max_list_requests = 0;
    });

    auto request = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "?list-type=2",
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ(0, fixture.controller->list_objects_calls);
    const auto ledger = vh::storage::s3::pricing::PriceBudgetService{}.listLedger(
        10,
        fixture.vault_id,
        fixture.secret.credential.id);
    EXPECT_TRUE(ledger.empty());
}

TEST(S3CostSafetyTest, GatewayLocalMaterializedGetDoesNotConsumeUpstreamBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-local-get-no-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    constexpr std::string_view objectKey = "local-get.txt";
    const vh::protocols::s3::Router router;
    auto put = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret,
        "local payload");
    ASSERT_EQ(router.route(std::move(put)).result(), http::status::ok);
    setGatewayRouteRequestBudget(fixture, [](auto& budget) {
        budget.max_get_requests = 0;
    });

    auto get = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);
    const auto response = router.route(std::move(get));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ("local payload", response.body());
    EXPECT_EQ(0, fixture.controller->download_to_buffer_calls);
    expectGatewayLedgerAbsent(fixture, "GetObject", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayLocalMaterializedGetCanCommitSyntheticGatewayBudgetWhenEnabled) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-local-get-synthetic",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000",
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        true);
    constexpr std::string_view objectKey = "local-get-synthetic.txt";
    const vh::protocols::s3::Router router;
    auto put = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret,
        "local payload");
    ASSERT_EQ(router.route(std::move(put)).result(), http::status::ok);

    auto get = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);
    const auto response = router.route(std::move(get));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ(0, fixture.controller->download_to_buffer_calls);
    expectGatewayLedgerCommitted(fixture, "GetObject", std::string(objectKey), true, "local_cache");
}

TEST(S3CostSafetyTest, GatewayPureLocalBucketWithLocalEnforcementConsumesSyntheticKeyBudgetAndCanDeny) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupLocalGatewayRouteBudgetFixture(
        "route-pure-local-synthetic",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "0.00000001",
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        true);
    saveGenericBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::Global,
        std::nullopt,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "1.00000000");
    saveGenericBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::Vault,
        std::nullopt,
        fixture.vault_id,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "1.00000000");

    constexpr std::string_view objectKey = "pure-local-budget.txt";
    const vh::protocols::s3::Router router;
    auto put = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret,
        "local payload");
    const auto putResponse = router.route(std::move(put));
    ASSERT_EQ(putResponse.result(), http::status::ok) << putResponse.body();

    expectGatewayLedgerCommitted(fixture, "PutObject", std::string(objectKey), true, "sync_deferred");
    const auto afterPutLedger = vh::storage::s3::pricing::PriceBudgetService{}.listLedger(
        20,
        fixture.vault_id,
        std::nullopt);
    ASSERT_FALSE(afterPutLedger.empty());
    EXPECT_TRUE(std::ranges::all_of(afterPutLedger, [&](const auto& entry) {
        return entry.gateway_credential_id &&
            *entry.gateway_credential_id == fixture.secret.credential.id &&
            entry.provider_key == "gateway-local" &&
            entry.synthetic;
    }));

    auto get = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);
    const auto denied = router.route(std::move(get));

    EXPECT_EQ(denied.result(), http::status::forbidden);
    EXPECT_NE(std::string::npos, denied.body().find("<Code>AccessDenied</Code>"));
    EXPECT_NE(std::string::npos, denied.body().find("S3 gateway synthetic local budget exceeded"));
    EXPECT_NE(std::string::npos, denied.body().find("provider_key=gateway-local"));
    EXPECT_NE(std::string::npos, denied.body().find("operation=GetObject"));
}

TEST(S3CostSafetyTest, GatewayRemoteDeleteLocalFirstDoesNotCommitCredentialBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-delete-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    constexpr std::string_view objectKey = "remote-delete.txt";
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"remote-delete-etag\"",
        .size_bytes = 42,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });
    vh::db::query::sync::RemoteObjectIndex::upsertFile(
        fixture.vault_id,
        remoteFile(std::string(objectKey)),
        "manifest");

    auto request = signedGatewayRouteRequest(
        http::verb::delete_,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::no_content) << response.body();
    EXPECT_EQ(0, fixture.controller->delete_object_calls);
    EXPECT_FALSE(vh::db::query::s3::Gateway::getObjectState(fixture.vault_id, std::string(objectKey)));
    const auto indexed = vh::db::query::sync::RemoteObjectIndex::listFilesForVault(fixture.vault_id);
    EXPECT_TRUE(std::ranges::any_of(indexed, [&](const auto& file) {
        return file && file->path.generic_string() == std::string("/") + std::string(objectKey);
    }));
    const auto actor = vh::db::query::identities::User::getUserById(fixture.secret.credential.principal_user_id);
    ASSERT_TRUE(actor);
    const vh::protocols::s3::ObjectStore objectStore;
    const auto listed = objectStore.listObjects(objectStore.resolveBucket(fixture.bucket_name, actor), {});
    EXPECT_TRUE(listed.objects.empty());
    EXPECT_EQ(1u, countGatewaySyncOriginForDbTest(fixture.vault_id, std::string(objectKey), "delete"));
    expectGatewayLedgerAbsent(fixture, "DeleteObject", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayRemoteDeleteLocalFirstCanCommitSyntheticGatewayBudgetWhenEnabled) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-delete-synthetic",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000",
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        true);
    constexpr std::string_view objectKey = "remote-delete-synthetic.txt";
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"remote-delete-synthetic\"",
        .size_bytes = 42,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    auto request = signedGatewayRouteRequest(
        http::verb::delete_,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::no_content) << response.body();
    EXPECT_EQ(0, fixture.controller->delete_object_calls);
    EXPECT_EQ(1u, countGatewaySyncOriginForDbTest(fixture.vault_id, std::string(objectKey), "delete"));
    expectGatewayLedgerCommitted(fixture, "DeleteObject", std::string(objectKey), true, "sync_deferred");
}

TEST(S3CostSafetyTest, GatewayRemoteMultipartLocalFirstDoesNotCommitCredentialBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-multipart-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    constexpr std::string_view objectKey = "multipart-object.txt";

    const vh::protocols::s3::Router router;
    auto initiate = signedGatewayRouteRequest(
        http::verb::post,
        "/" + fixture.bucket_name + "/" + std::string(objectKey) + "?uploads",
        fixture.secret);
    const auto initiateResponse = router.route(std::move(initiate));
    ASSERT_EQ(initiateResponse.result(), http::status::ok) << initiateResponse.body();
    const auto uploadId = textBetween(initiateResponse.body(), "<UploadId>", "</UploadId>");
    ASSERT_FALSE(uploadId.empty());

    auto uploadPart = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/" + std::string(objectKey) + "?partNumber=1&uploadId=" + uploadId,
        fixture.secret,
        "multipart payload");
    const auto uploadPartResponse = router.route(std::move(uploadPart));
    ASSERT_EQ(uploadPartResponse.result(), http::status::ok) << uploadPartResponse.body();
    const auto partEtag = std::string(uploadPartResponse[http::field::etag]);
    ASSERT_FALSE(partEtag.empty());

    const auto completeBody =
        "<CompleteMultipartUpload><Part><PartNumber>1</PartNumber><ETag>" +
        partEtag +
        "</ETag></Part></CompleteMultipartUpload>";
    auto complete = signedGatewayRouteRequest(
        http::verb::post,
        "/" + fixture.bucket_name + "/" + std::string(objectKey) + "?uploadId=" + uploadId,
        fixture.secret,
        completeBody);
    const auto completeResponse = router.route(std::move(complete));
    EXPECT_EQ(completeResponse.result(), http::status::ok) << completeResponse.body();
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    EXPECT_TRUE(vh::db::query::fs::File::getFileByPath(fixture.vault_id, "/" + std::string(objectKey)));
    ASSERT_TRUE(vh::db::query::s3::Gateway::getObjectState(fixture.vault_id, std::string(objectKey)));

    expectGatewayLedgerAbsent(fixture, "CreateMultipartUpload", std::string(objectKey));
    expectGatewayLedgerAbsent(fixture, "UploadPart", std::string(objectKey));
    expectGatewayLedgerAbsent(fixture, "CompleteMultipartUpload", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayRemoteCopyRoutePreflightsAndCommitsCopyBudget) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-copy-budget",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = fixture.vault_id,
        .bucket_name = fixture.bucket_name,
        .api_exclusive = true,
        .mode = "remote_proxy",
        .created_by = fixture.secret.credential.principal_user_id
    });
    constexpr std::string_view sourceKey = "copy-source.txt";
    constexpr std::string_view destKey = "copy-dest.txt";
    fixture.controller->download_payloads.push_back({'c', 'o', 'p', 'y'});
    const auto remoteManifest = vh::sync::model::remote_manifest::buildIndexV1(fixture.vault_id, {});
    fixture.controller->download_payloads.emplace_back(remoteManifest.begin(), remoteManifest.end());
    fixture.controller->head_response = std::unordered_map<std::string, std::string>{
        {"x-amz-meta-vh-encrypted", "false"}
    };
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(sourceKey),
        .etag = "\"copy-source-etag\"",
        .size_bytes = 4,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    vh::protocols::s3::Router::Request request{
        http::verb::put,
        "/" + fixture.bucket_name + "/" + std::string(destKey),
        11};
    request.set(http::field::host, "localhost:39000");
    request.set("x-amz-copy-source", "/" + fixture.bucket_name + "/" + std::string(sourceKey));
    request.prepare_payload();
    signGatewayRouteRequest(request, fixture.secret.credential.access_key, fixture.secret.secret_access_key);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ(1, fixture.controller->download_to_buffer_calls);
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    EXPECT_TRUE(vh::db::query::fs::File::getFileByPath(fixture.vault_id, "/" + std::string(destKey)));
    expectGatewayLedgerAbsent(fixture, "GetObject", std::string(sourceKey));
    expectGatewayLedgerCommitted(fixture, "CopyObject", std::string(sourceKey));
}

TEST(S3CostSafetyTest, GatewayRemoteUploadPartFailureBeforeSyncDoesNotReserveBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-upload-part-release",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    constexpr std::string_view objectKey = "multipart-release.txt";

    const vh::protocols::s3::Router router;
    auto initiate = signedGatewayRouteRequest(
        http::verb::post,
        "/" + fixture.bucket_name + "/" + std::string(objectKey) + "?uploads",
        fixture.secret);
    const auto initiateResponse = router.route(std::move(initiate));
    ASSERT_EQ(initiateResponse.result(), http::status::ok) << initiateResponse.body();
    const auto uploadId = textBetween(initiateResponse.body(), "<UploadId>", "</UploadId>");
    ASSERT_FALSE(uploadId.empty());

    auto uploadPart = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/" + std::string(objectKey) + "?partNumber=1&uploadId=" + uploadId,
        fixture.secret,
        "bad checksum body");
    uploadPart.set("content-md5", "AAAAAAAAAAAAAAAAAAAAAA==");
    const auto uploadPartResponse = router.route(std::move(uploadPart));

    EXPECT_EQ(uploadPartResponse.result(), http::status::bad_request);
    EXPECT_NE(std::string::npos, uploadPartResponse.body().find("<Code>BadDigest</Code>"));
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    expectGatewayLedgerAbsent(fixture, "UploadPart", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayRemotePutLocalFirstIgnoresUpstreamRequestBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-put-request-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    setGatewayRouteRequestBudget(fixture, [](auto& budget) {
        budget.max_put_requests = 0;
    });

    auto request = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/request-budget-denied.txt",
        fixture.secret,
        "payload");

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    EXPECT_TRUE(vh::db::query::fs::File::getFileByPath(fixture.vault_id, "/request-budget-denied.txt"));
    expectGatewayLedgerAbsent(fixture, "PutObject", "request-budget-denied.txt");
}

TEST(S3CostSafetyTest, GatewayRemoteGetRequestBudgetDeniedBeforeRemoteDownload) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-get-request-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    setGatewayRouteRequestBudget(fixture, [](auto& budget) {
        budget.max_get_requests = 0;
    });
    constexpr std::string_view objectKey = "request-budget-get.txt";
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"request-budget-get\"",
        .size_bytes = 128,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    auto request = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::service_unavailable);
    EXPECT_NE(std::string::npos, response.body().find("<Code>SlowDown</Code>"));
    EXPECT_NE(std::string::npos, response.body().find("kind=GET"));
    EXPECT_EQ(0, fixture.controller->download_to_buffer_calls);
    expectGatewayLedgerAbsent(fixture, "GetObject", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayRemoteGetDownloadedBytesBudgetDeniedDuringRemoteDownload) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-get-byte-request-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    setGatewayRouteRequestBudget(fixture, [](auto& budget) {
        budget.max_get_requests = 1;
        budget.max_downloaded_bytes = 4;
    });
    constexpr std::string_view objectKey = "request-budget-bytes.txt";
    fixture.controller->download_payload.assign(5, static_cast<uint8_t>('x'));
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"request-budget-bytes\"",
        .size_bytes = 5,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    auto request = signedGatewayRouteRequest(
        http::verb::get,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::service_unavailable);
    EXPECT_NE(std::string::npos, response.body().find("<Code>SlowDown</Code>"));
    EXPECT_NE(std::string::npos, response.body().find("kind=downloaded bytes"));
    EXPECT_EQ(1, fixture.controller->download_to_buffer_calls);
    expectGatewayLedgerAbsent(fixture, "GetObject", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewayRemoteDeleteLocalFirstIgnoresUpstreamRequestBudgetByDefault) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-delete-request-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "1.00000000");
    setGatewayRouteRequestBudget(fixture, [](auto& budget) {
        budget.max_delete_requests = 0;
    });
    constexpr std::string_view objectKey = "request-budget-delete.txt";
    vh::db::query::s3::Gateway::upsertObject({
        .vault_id = fixture.vault_id,
        .object_key = std::string(objectKey),
        .etag = "\"request-budget-delete\"",
        .size_bytes = 42,
        .content_type = "text/plain",
        .storage_class = std::nullopt,
        .last_modified = std::time(nullptr),
        .multipart = false,
        .part_count = std::nullopt
    });

    auto request = signedGatewayRouteRequest(
        http::verb::delete_,
        "/" + fixture.bucket_name + "/" + std::string(objectKey),
        fixture.secret);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::no_content) << response.body();
    EXPECT_EQ(0, fixture.controller->delete_object_calls);
    EXPECT_FALSE(vh::db::query::s3::Gateway::getObjectState(fixture.vault_id, std::string(objectKey)));
    expectGatewayLedgerAbsent(fixture, "DeleteObject", std::string(objectKey));
}

TEST(S3CostSafetyTest, GatewaySyntheticLocalBudgetDeniedReturnsXmlAccessDeniedBeforeLocalPut) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        "0.00000000",
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        true);

    auto request = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/denied-object.txt",
        fixture.secret,
        "payload");

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::forbidden);
    EXPECT_NE(std::string::npos, response.body().find("<Code>AccessDenied</Code>"));
    EXPECT_NE(std::string::npos, response.body().find("S3 gateway synthetic local budget exceeded"));
    EXPECT_NE(std::string::npos, response.body().find("scope=gateway_credential_vault"));
    EXPECT_NE(std::string::npos, response.body().find("policy_id="));
    EXPECT_NE(std::string::npos, response.body().find("window=monthly"));
    EXPECT_NE(std::string::npos, response.body().find("provider_key=gateway-local"));
    EXPECT_NE(std::string::npos, response.body().find("vault_id=" + std::to_string(fixture.vault_id)));
    EXPECT_NE(std::string::npos, response.body().find("gateway_credential_id=" + std::to_string(fixture.secret.credential.id)));
    EXPECT_NE(std::string::npos, response.body().find("operation=PutObject"));
    EXPECT_NE(std::string::npos, response.body().find("request_uuid="));
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    EXPECT_EQ(0, fixture.controller->upload_object_with_metadata_calls);
    EXPECT_FALSE(vh::db::query::fs::File::getFileByPath(fixture.vault_id, "/denied-object.txt"));
    const auto ledger = vh::storage::s3::pricing::PriceBudgetService{}.listLedger(
        10,
        fixture.vault_id,
        fixture.secret.credential.id);
    EXPECT_TRUE(ledger.empty());
}

TEST(S3CostSafetyTest, GatewaySyntheticLocalKeyBudgetDeniedReturnsPolicyScopeWindowInXml) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway route budget test due to missing environment variables.";
    S3CostConfigRestore restoreConfig(vh::config::Registry::get());
    configureS3GatewayRouteBudgetConfig();

    auto fixture = setupGatewayRouteBudgetFixture(
        "route-key-budget-denied",
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        "0.00000000",
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        true);

    auto request = signedGatewayRouteRequest(
        http::verb::put,
        "/" + fixture.bucket_name + "/denied-key-budget.txt",
        fixture.secret,
        "payload");

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::forbidden);
    EXPECT_NE(std::string::npos, response.body().find("<Code>AccessDenied</Code>"));
    EXPECT_NE(std::string::npos, response.body().find("S3 gateway synthetic local budget exceeded"));
    EXPECT_NE(std::string::npos, response.body().find("scope=gateway_credential"));
    EXPECT_NE(std::string::npos, response.body().find("policy_id="));
    EXPECT_NE(std::string::npos, response.body().find("window=monthly"));
    EXPECT_NE(std::string::npos, response.body().find("operation=PutObject"));
    EXPECT_EQ(0, fixture.controller->upload_buffer_with_metadata_calls);
    EXPECT_FALSE(vh::db::query::fs::File::getFileByPath(fixture.vault_id, "/denied-key-budget.txt"));
}

} // namespace vh::test::s3_cost_safety
