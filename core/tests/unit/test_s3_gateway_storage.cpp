// S3 gateway, part 3/3 (S3GatewayDbTest): bucket naming and bindings, credential validity and scope
// shorthand, multipart uploads, deletes, remote-index listing and the DB-backed gateway service.

#include "support/s3_gateway_fixture.hpp"

namespace vh::test::s3_gateway {

TEST_F(S3GatewayDbTest, VaultSlugDefaultsAndDisplayRenameDoesNotRewriteBindings) {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    session->user = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(session->user);
    ASSERT_TRUE(session->user->isSuperAdmin());

    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->name = "My Photos";
    vault->description = "Slug default test";
    vault->owner_id = userId;
    vault->type = vh::vault::model::VaultType::Local;
    vault->is_active = true;

    auto sync = std::make_shared<vh::sync::model::LocalPolicy>();
    sync->conflict_policy = vh::sync::model::LocalPolicy::ConflictPolicy::KeepBoth;

    vault = vh::runtime::Deps::get().storageManager->addVault(vault, sync);
    ASSERT_TRUE(vault);
    EXPECT_EQ("my-photos", vault->slug);
    EXPECT_EQ("my-photos", vault->effectiveFuseName());
    const auto mountPoint = vault->mount_point.string();
    const auto initialFuseRoot = vh::runtime::Deps::get().storageManager
        ->getEngine(vault->id)
        ->paths
        ->absRelToRoot(vh::runtime::Deps::get().storageManager->getEngine(vault->id)->paths->vaultRoot,
                       vh::fs::model::PathType::FUSE_ROOT);
    EXPECT_EQ(initialFuseRoot, std::filesystem::path("/my-photos"));

    const auto bound = vh::protocols::ws::handler::S3Gateway::bucketsBind({
        {"vault_id", vault->id}
    }, session);
    EXPECT_TRUE(bound.at("bound").get<bool>());
    ASSERT_TRUE(vh::db::query::s3::Gateway::resolveBucket("my-photos"));

    auto renamed = vh::db::query::vault::Vault::getVault(vault->id);
    ASSERT_TRUE(renamed);
    renamed->name = "Archive Photos";
    vh::runtime::Deps::get().storageManager->updateVault(renamed);

    const auto reloaded = vh::db::query::vault::Vault::getVault(vault->id);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ("Archive Photos", reloaded->name);
    EXPECT_EQ("my-photos", reloaded->slug);
    EXPECT_EQ("my-photos", reloaded->effectiveFuseName());
    EXPECT_EQ(mountPoint, reloaded->mount_point.string());
    ASSERT_TRUE(vh::db::query::s3::Gateway::resolveBucket("my-photos"));
}

TEST_F(S3GatewayDbTest, SlugAndFuseOverrideControlOnlyDefaultFuseBinding) {
    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->name = "Slug Update " + s3GatewayUniqueSuffix("vault");
    vault->description = "Slug update test";
    vault->owner_id = userId;
    vault->type = vh::vault::model::VaultType::Local;
    vault->is_active = true;

    auto sync = std::make_shared<vh::sync::model::LocalPolicy>();
    sync->conflict_policy = vh::sync::model::LocalPolicy::ConflictPolicy::KeepBoth;

    vault = vh::runtime::Deps::get().storageManager->addVault(vault, sync);
    ASSERT_TRUE(vault);

    auto update = vh::db::query::vault::Vault::getVault(vault->id);
    ASSERT_TRUE(update);
    update->slug = "renamed-default-" + std::to_string(vault->id);
    vh::runtime::Deps::get().storageManager->updateVault(update);

    auto reloaded = vh::db::query::vault::Vault::getVault(vault->id);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(update->slug, reloaded->effectiveFuseName());
    EXPECT_EQ(
        std::filesystem::path("/") / update->slug,
        vh::runtime::Deps::get().storageManager->getEngine(vault->id)->paths->absRelToRoot(
            vh::runtime::Deps::get().storageManager->getEngine(vault->id)->paths->vaultRoot,
            vh::fs::model::PathType::FUSE_ROOT));

    reloaded->fuse_name = "custom-root-" + std::to_string(vault->id);
    vh::runtime::Deps::get().storageManager->updateVault(reloaded);
    auto overridden = vh::db::query::vault::Vault::getVault(vault->id);
    ASSERT_TRUE(overridden);
    EXPECT_EQ(*reloaded->fuse_name, overridden->effectiveFuseName());

    overridden->slug = "slug-after-override-" + std::to_string(vault->id);
    vh::runtime::Deps::get().storageManager->updateVault(overridden);
    auto afterSlugUpdate = vh::db::query::vault::Vault::getVault(vault->id);
    ASSERT_TRUE(afterSlugUpdate);
    EXPECT_EQ("slug-after-override-" + std::to_string(vault->id), afterSlugUpdate->slug);
    EXPECT_EQ(*reloaded->fuse_name, afterSlugUpdate->effectiveFuseName());
    EXPECT_FALSE(vh::db::query::s3::Gateway::resolveBucket(afterSlugUpdate->slug));
}

TEST_F(S3GatewayDbTest, ExplicitS3BucketBindingSurvivesVaultNameAndSlugUpdates) {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    session->user = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(session->user);
    ASSERT_TRUE(session->user->isSuperAdmin());

    const auto targetVaultId = createLocalVault(s3GatewayUniqueSuffix("explicit_bucket"), userId);
    const auto bucketName = "explicit-binding-" + std::to_string(targetVaultId);
    const auto bound = vh::protocols::ws::handler::S3Gateway::bucketsBind({
        {"vault_id", targetVaultId},
        {"bucket_name", bucketName}
    }, session);
    EXPECT_TRUE(bound.at("bound").get<bool>());

    auto vault = vh::db::query::vault::Vault::getVault(targetVaultId);
    ASSERT_TRUE(vault);
    vault->name = "Renamed Explicit Bucket Vault";
    vault->slug = "renamed-explicit-" + std::to_string(targetVaultId);
    vh::runtime::Deps::get().storageManager->updateVault(vault);

    ASSERT_TRUE(vh::db::query::s3::Gateway::resolveBucket(bucketName));
    EXPECT_FALSE(vh::db::query::s3::Gateway::resolveBucket(vault->slug));
}

TEST_F(S3GatewayDbTest, RejectsInvalidAndDuplicateExternalNames) {
    auto vault = std::make_shared<vh::vault::model::Vault>();
    vault->name = "Invalid Slug " + s3GatewayUniqueSuffix("vault");
    vault->slug = "Invalid_Slug";
    vault->owner_id = userId;
    vault->type = vh::vault::model::VaultType::Local;

    auto sync = std::make_shared<vh::sync::model::LocalPolicy>();
    EXPECT_THROW((void)vh::db::query::vault::Vault::upsertVault(vault, sync), std::invalid_argument);

    auto first = std::make_shared<vh::vault::model::Vault>();
    first->name = "Duplicate Slug A " + s3GatewayUniqueSuffix("vault");
    first->slug = uniqueS3Name("duplicate-slug");
    first->owner_id = userId;
    first->type = vh::vault::model::VaultType::Local;
    const auto duplicateSlug = first->slug;
    first->id = vh::db::query::vault::Vault::upsertVault(first, std::make_shared<vh::sync::model::LocalPolicy>());

    auto second = std::make_shared<vh::vault::model::Vault>();
    second->name = "Duplicate Slug B " + s3GatewayUniqueSuffix("vault");
    second->slug = duplicateSlug;
    second->owner_id = userId;
    second->type = vh::vault::model::VaultType::Local;
    EXPECT_THROW((void)vh::db::query::vault::Vault::upsertVault(second, std::make_shared<vh::sync::model::LocalPolicy>()), std::invalid_argument);

    auto fuseA = vh::db::query::vault::Vault::getVault(first->id);
    ASSERT_TRUE(fuseA);
    fuseA->fuse_name = "shared-fuse-" + std::to_string(first->id);
    vh::db::query::vault::Vault::upsertVault(fuseA);

    auto fuseB = std::make_shared<vh::vault::model::Vault>();
    fuseB->name = "Duplicate Fuse " + s3GatewayUniqueSuffix("vault");
    fuseB->slug = uniqueS3Name("duplicate-fuse");
    fuseB->fuse_name = fuseA->fuse_name;
    fuseB->owner_id = userId;
    fuseB->type = vh::vault::model::VaultType::Local;
    EXPECT_THROW((void)vh::db::query::vault::Vault::upsertVault(fuseB, std::make_shared<vh::sync::model::LocalPolicy>()), std::invalid_argument);

    EXPECT_THROW(vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = "Bad_Bucket",
        .api_exclusive = false,
        .mode = "local",
        .created_by = userId
    }), std::invalid_argument);
}

TEST_F(S3GatewayDbTest, DisabledAndExpiredCredentialsDoNotAuthenticate) {
    const vh::protocols::s3::CredentialManager manager;
    auto active = manager.createCredential({
        .created_by = userId,
        .principal_user_id = userId,
        .name = "disabled-auth-" + s3GatewayUniqueSuffix("credential"),
        .scope_mode = "user_access",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {}
    });
    ASSERT_TRUE(manager.findEnabledSecret(active.credential.access_key));

    vh::db::Transactions::exec("S3GatewayDbTest::disableCredential", [&](pqxx::work& txn) {
        txn.exec(
            "UPDATE s3_gateway_credentials SET enabled = FALSE WHERE id = $1",
            pqxx::params{active.credential.id});
    });
    EXPECT_FALSE(manager.findEnabledSecret(active.credential.access_key));

    auto expired = manager.createCredential({
        .created_by = userId,
        .principal_user_id = userId,
        .name = "expired-auth-" + s3GatewayUniqueSuffix("credential"),
        .scope_mode = "user_access",
        .description = std::nullopt,
        .expires_at = std::time(nullptr) - 60,
        .vault_scopes = {}
    });
    EXPECT_FALSE(manager.findEnabledSecret(expired.credential.access_key));
}

TEST_F(S3GatewayDbTest, CredentialScopeShorthandWritesFinalRbacTablesAndGatesActions) {
    const auto secondVaultId = createLocalVault(s3GatewayUniqueSuffix("scope"), userId);

    vh::db::query::s3::GatewayCredential credential;
    credential.user_id = userId;
    credential.principal_user_id = userId;
    credential.created_by = userId;
    credential.name = "scope-query-" + s3GatewayUniqueSuffix("credential");
    credential.access_key = "VHTESTSCOPEQUERY" + std::to_string(vaultId);
    credential.encrypted_secret_access_key = {1, 2, 3};
    credential.iv = {4, 5, 6};
    credential.enabled = true;
    credential.scope_mode = "vault_allowlist";
    credential.id = vh::db::query::s3::Gateway::createCredential(credential);

    vh::db::query::s3::Gateway::replaceCredentialScopeShorthand(credential.id, {
        {
            .credential_id = credential.id,
            .vault_id = vaultId,
            .can_list = true,
            .can_read = true,
            .can_write = false,
            .can_delete = false,
            .can_admin = false
        },
        {
            .credential_id = credential.id,
            .vault_id = secondVaultId,
            .can_list = true,
            .can_read = false,
            .can_write = true,
            .can_delete = true,
            .can_admin = false
        }
    });

    auto selectedVaults = vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential.id);
    ASSERT_EQ(selectedVaults.size(), 2u);
    auto defaultRole = vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    ASSERT_TRUE(defaultRole);
    EXPECT_EQ(defaultRole->vault_role_id, roleIdByName("implicit_deny"));
    auto assignments = vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    ASSERT_EQ(assignments.size(), 2u);

    vh::db::query::s3::Gateway::replaceCredentialScopeShorthand(credential.id, {{
        .credential_id = credential.id,
        .vault_id = secondVaultId,
        .can_list = true,
        .can_read = true,
        .can_write = true,
        .can_delete = false,
        .can_admin = false
    }});
    selectedVaults = vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential.id);
    ASSERT_EQ(selectedVaults.size(), 1u);
    EXPECT_EQ(selectedVaults.front().vault_id, secondVaultId);
    assignments = vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    EXPECT_TRUE(assignments.empty());
    defaultRole = vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    ASSERT_TRUE(defaultRole);
    EXPECT_EQ(defaultRole->vault_role_id, roleIdByName("contributor"));
    const auto credentialRole = vh::db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, secondVaultId);
    EXPECT_FALSE(credentialRole);
    const auto effectiveRole = vh::db::query::s3::Gateway::getEffectiveCredentialVaultRole(credential.id, secondVaultId, credential.scope_mode);
    ASSERT_TRUE(effectiveRole);
    EXPECT_EQ(effectiveRole->name, "contributor");

    vh::db::Transactions::exec("S3GatewayDbTest::assignPrincipalGatewayScopeVaultRole", [&](pqxx::work& txn) {
        const auto roleId = txn.exec(
            "SELECT id FROM vault_role WHERE name = 'manager' LIMIT 1"
        ).one_field().as<uint32_t>();
        txn.exec(
            "INSERT INTO vault_role_assignments (vault_id, subject_type, subject_id, role_id) "
            "VALUES ($1, 'user', $2, $3) "
            "ON CONFLICT (vault_id, subject_type, subject_id) DO UPDATE SET role_id = EXCLUDED.role_id",
            pqxx::params{secondVaultId, userId, roleId});
    });

    auto user = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(user);
    vh::protocols::s3::AuthContext auth{
        .user = user,
        .credential = credential,
        .credential_id = credential.id,
        .access_key = credential.access_key,
        .scope_mode = "vault_allowlist",
        .dev_context = false
    };
    using Action = vh::rbac::permission::vault::FilesystemAction;
    EXPECT_TRUE(vh::protocols::s3::ObjectStore::credentialAllows(auth, secondVaultId, Action::Write));
    EXPECT_FALSE(vh::protocols::s3::ObjectStore::credentialAllows(auth, secondVaultId, Action::Delete));
    EXPECT_FALSE(vh::protocols::s3::ObjectStore::credentialAllows(auth, vaultId, Action::Read));
}

TEST_F(S3GatewayDbTest, SignedRouteScopeDeniedReturnsS3XmlAccessDenied) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.require_sigv4 = true;
    cfg.s3_gateway.allow_path_style = true;
    vh::config::Registry::set(cfg);

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const std::string bucketName = "scope-denied-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "local",
        .created_by = admin->id
    });

    const auto selectedOtherVaultId = createLocalVault(s3GatewayUniqueSuffix("scope_denied_selected"), admin->id);
    const vh::protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential({
        .created_by = admin->id,
        .principal_user_id = admin->id,
        .name = "route-scope-denied-" + s3GatewayUniqueSuffix("credential"),
        .scope_mode = "vault_allowlist",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .default_vault_role_id = roleIdByName("reader"),
        .selected_vault_ids = {selectedOtherVaultId},
        .vault_scopes = {}
    });

    vh::protocols::s3::Router::Request request{http::verb::head, "/" + bucketName, 11};
    request.set(http::field::host, "localhost:39000");
    signS3GatewayRequest(request, secret.credential.access_key, secret.secret_access_key);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::forbidden);
    EXPECT_NE(response.body().find("<Code>AccessDenied</Code>"), std::string::npos);
    EXPECT_NE(response.body().find("<RequestId>"), std::string::npos);
}

TEST_F(S3GatewayDbTest, MultipartUploadUsesOpaquePartDirAndCompleteKeepsRoot) {
    std::filesystem::remove_all(vh::protocols::s3::MultipartStore::partRoot());

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);

    const std::string bucketName = "multipart-complete-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "local",
        .created_by = admin->id
    });
    const vh::protocols::s3::ObjectStore objects;
    const auto bucket = objects.resolveBucket(bucketName, admin);

    const vh::protocols::s3::MultipartStore store;
    const std::string key = "very-secret-object.txt";
    const auto uploadId = store.createUpload(bucket, key, {});
    const auto upload = vh::db::query::s3::Gateway::getMultipartUpload(uploadId);
    ASSERT_TRUE(upload);
    EXPECT_FALSE(upload->parts_dir_id.empty());
    EXPECT_NE(upload->parts_dir_id, upload->upload_id);

    const auto part = store.uploadPart(bucket, key, uploadId, 1, {'p', 'a', 'r', 't'});
    const auto partDir = vh::protocols::s3::MultipartStore::partRoot() / upload->parts_dir_id;
    EXPECT_EQ(part.path, partDir / "1");
    EXPECT_TRUE(std::filesystem::exists(part.path));
    EXPECT_EQ(part.path.string().find(bucketName), std::string::npos);
    EXPECT_EQ(part.path.string().find(key), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(vh::protocols::s3::MultipartStore::partRoot()));

    const auto dirPerms = std::filesystem::status(partDir).permissions() &
        (std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all);
    EXPECT_EQ(dirPerms, std::filesystem::perms::owner_all);

    const auto state = store.completeUpload(bucket, key, uploadId, {{1, part.etag}});
    EXPECT_TRUE(state.multipart);
    EXPECT_FALSE(std::filesystem::exists(partDir));
    EXPECT_TRUE(std::filesystem::exists(vh::protocols::s3::MultipartStore::partRoot()));

    std::filesystem::remove_all(vh::protocols::s3::MultipartStore::partRoot());
}

TEST_F(S3GatewayDbTest, AbortMultipartUploadRemovesOpaquePartDirAndKeepsRoot) {
    std::filesystem::remove_all(vh::protocols::s3::MultipartStore::partRoot());

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);

    const std::string bucketName = "multipart-abort-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "local",
        .created_by = admin->id
    });
    const vh::protocols::s3::ObjectStore objects;
    const auto bucket = objects.resolveBucket(bucketName, admin);

    const vh::protocols::s3::MultipartStore store;
    const std::string key = "abort-secret-object.txt";
    const auto uploadId = store.createUpload(bucket, key, {});
    const auto upload = vh::db::query::s3::Gateway::getMultipartUpload(uploadId);
    ASSERT_TRUE(upload);

    const auto part = store.uploadPart(bucket, key, uploadId, 1, {'p', 'a', 'r', 't'});
    const auto partDir = vh::protocols::s3::MultipartStore::partRoot() / upload->parts_dir_id;
    ASSERT_TRUE(std::filesystem::exists(part.path));

    store.abortUpload(bucket, key, uploadId);

    EXPECT_FALSE(vh::db::query::s3::Gateway::getMultipartUpload(uploadId));
    EXPECT_TRUE(vh::db::query::s3::Gateway::listMultipartParts(uploadId).empty());
    EXPECT_FALSE(std::filesystem::exists(partDir));
    EXPECT_TRUE(std::filesystem::exists(vh::protocols::s3::MultipartStore::partRoot()));

    std::filesystem::remove_all(vh::protocols::s3::MultipartStore::partRoot());
}

TEST_F(S3GatewayDbTest, AbortExpiredMultipartUploadsUsesConfiguredRetention) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    std::filesystem::remove_all(vh::protocols::s3::MultipartStore::partRoot());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.multipart.abort_after_days = 1;
    vh::config::Registry::set(cfg);

    const std::string oldUpload = "old-upload-" + std::to_string(vaultId);
    const std::string freshUpload = "fresh-upload-" + std::to_string(vaultId);
    const std::string oldPartsDirId = "old-parts-" + std::to_string(vaultId);
    const std::string freshPartsDirId = "fresh-parts-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::createMultipartUpload({
        .upload_id = oldUpload,
        .parts_dir_id = oldPartsDirId,
        .vault_id = vaultId,
        .object_key = "old.txt",
        .initiated_by = userId,
        .content_type = std::nullopt,
        .metadata = {},
        .storage_class = std::nullopt
    });
    vh::db::query::s3::Gateway::createMultipartUpload({
        .upload_id = freshUpload,
        .parts_dir_id = freshPartsDirId,
        .vault_id = vaultId,
        .object_key = "fresh.txt",
        .initiated_by = userId,
        .content_type = std::nullopt,
        .metadata = {},
        .storage_class = std::nullopt
    });

    const auto oldPartDir = vh::protocols::s3::MultipartStore::partRoot() / oldPartsDirId;
    const auto oldPartPath = oldPartDir / "1";
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
    EXPECT_FALSE(std::filesystem::exists(oldPartDir));
    EXPECT_TRUE(std::filesystem::exists(vh::protocols::s3::MultipartStore::partRoot()));
    EXPECT_TRUE(vh::db::query::s3::Gateway::getMultipartUpload(freshUpload));

    std::filesystem::remove_all(vh::protocols::s3::MultipartStore::partRoot());
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
        .actor = vh::db::query::identities::User::getUserById(userId),
        .gateway_access = std::nullopt
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

TEST_F(S3GatewayDbTest, ObjectStoreLocalListUsesMetadataEtagsForFilesystemEntries) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const auto localVaultId = createLocalVault(s3GatewayUniqueSuffix("metadata_list_local"), admin->id);
    const auto engine = vh::runtime::Deps::get().storageManager->getEngine(localVaultId);
    ASSERT_TRUE(engine);
    const std::filesystem::path vaultPath = "/metadata-only-list.txt";
    auto created = vh::fs::Filesystem::createFile({
        .path = vaultPath,
        .fuse_path = engine->vaultPathToFusePath(vaultPath),
        .buffer = {'p', 'l', 'a', 'i', 'n', 't', 'e', 'x', 't'},
        .engine = engine,
        .user = admin,
        .overwrite = true
    });
    ASSERT_TRUE(created);

    const auto bucketName = "metadata-list-" + std::to_string(localVaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = localVaultId,
        .bucket_name = bucketName,
        .api_exclusive = false,
        .mode = "local",
        .created_by = admin->id
    });

    const vh::protocols::s3::ObjectStore store;
    const auto bucket = store.resolveBucket(bucketName, admin);
    const auto listed = store.listObjects(bucket, {});

    ASSERT_EQ(1u, listed.objects.size());
    EXPECT_EQ("metadata-only-list.txt", listed.objects.front().object_key);
    EXPECT_TRUE(listed.objects.front().etag.starts_with("\"vh-meta-"));
    EXPECT_NE("\"66a7fb99f149162da3d8c6c15c225d0f\"", listed.objects.front().etag);
}

TEST_F(S3GatewayDbTest, ObjectStoreRemoteListUsesRemoteIndexWithoutGatewayRows) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const auto remoteVaultId = createLocalVault(s3GatewayUniqueSuffix("metadata_list_remote"), admin->id);
    const auto bucketName = "remote-index-list-" + std::to_string(remoteVaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = remoteVaultId,
        .bucket_name = bucketName,
        .api_exclusive = false,
        .mode = "remote_cache",
        .created_by = admin->id
    });
    vh::db::Transactions::exec("S3GatewayDbTest::insertRemoteIndexListObject", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO remote_object_index (vault_id, object_key, size_bytes, etag, storage_class, source)
                VALUES ($1, $2, $3, $4, $5, $6)
            )SQL",
            pqxx::params{remoteVaultId, "indexed/only.txt", 12, "\"remote-index-etag\"", "STANDARD", "manifest"});
    });

    const vh::protocols::s3::ObjectStore store;
    const auto bucket = store.resolveBucket(bucketName, admin);
    const auto listed = store.listObjects(bucket, {});

    ASSERT_EQ(1u, listed.objects.size());
    EXPECT_EQ("indexed/only.txt", listed.objects.front().object_key);
    EXPECT_EQ("\"remote-index-etag\"", listed.objects.front().etag);
    EXPECT_EQ(12u, listed.objects.front().size_bytes);
    ASSERT_TRUE(listed.objects.front().storage_class);
    EXPECT_EQ("STANDARD", *listed.objects.front().storage_class);
}

TEST_F(S3GatewayDbTest, DeleteBucketRejectsLocalFilesystemEntriesWithoutGatewayRows) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const auto localVaultId = createLocalVault(s3GatewayUniqueSuffix("delete_bucket_local"), admin->id);
    const auto engine = vh::runtime::Deps::get().storageManager->getEngine(localVaultId);
    ASSERT_TRUE(engine);
    const std::filesystem::path vaultPath = "/only-in-fs.txt";
    ASSERT_TRUE(vh::fs::Filesystem::createFile({
        .path = vaultPath,
        .fuse_path = engine->vaultPathToFusePath(vaultPath),
        .buffer = {'l', 'o', 'c', 'a', 'l'},
        .engine = engine,
        .user = admin,
        .overwrite = true
    }));

    const auto bucketName = "delete-local-non-empty-" + std::to_string(localVaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = localVaultId,
        .bucket_name = bucketName,
        .api_exclusive = false,
        .mode = "local",
        .created_by = admin->id
    });

    const vh::protocols::s3::ObjectStore store;
    try {
        store.deleteBucket(bucketName, admin);
        FAIL() << "expected BucketNotEmpty";
    } catch (const vh::protocols::s3::S3Error& error) {
        EXPECT_EQ("BucketNotEmpty", error.code);
    }
    EXPECT_TRUE(vh::db::query::s3::Gateway::resolveBucket(bucketName));
}

TEST_F(S3GatewayDbTest, DeleteBucketRejectsRemoteIndexLiveObjects) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const auto remoteVaultId = createLocalVault(s3GatewayUniqueSuffix("delete_bucket_remote"), admin->id);
    const auto bucketName = "delete-remote-non-empty-" + std::to_string(remoteVaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = remoteVaultId,
        .bucket_name = bucketName,
        .api_exclusive = false,
        .mode = "remote_cache",
        .created_by = admin->id
    });
    vh::db::Transactions::exec("S3GatewayDbTest::insertRemoteIndexBucketDeleteObject", [&](pqxx::work& txn) {
        txn.exec(
            R"SQL(
                INSERT INTO remote_object_index (vault_id, object_key, size_bytes, etag, source)
                VALUES ($1, $2, $3, $4, $5)
            )SQL",
            pqxx::params{remoteVaultId, "live-remote.txt", 7, "\"live-remote\"", "manifest"});
    });

    const vh::protocols::s3::ObjectStore store;
    try {
        store.deleteBucket(bucketName, admin);
        FAIL() << "expected BucketNotEmpty";
    } catch (const vh::protocols::s3::S3Error& error) {
        EXPECT_EQ("BucketNotEmpty", error.code);
    }
    EXPECT_TRUE(vh::db::query::s3::Gateway::resolveBucket(bucketName));
}

TEST_F(S3GatewayDbTest, DeleteBucketAllowsTrulyEmptyApiExclusiveBucket) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const auto emptyVaultId = createLocalVault(s3GatewayUniqueSuffix("delete_bucket_empty"), admin->id);
    const auto bucketName = "delete-empty-" + std::to_string(emptyVaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = emptyVaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "local",
        .created_by = admin->id
    });

    const vh::protocols::s3::ObjectStore store;
    EXPECT_NO_THROW(store.deleteBucket(bucketName, admin));
    EXPECT_FALSE(vh::db::query::s3::Gateway::resolveBucket(bucketName));
}

} // namespace vh::test::s3_gateway
