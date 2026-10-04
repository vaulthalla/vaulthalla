// S3 gateway, part 2/3 (S3GatewayDbTest): list pagination, credential RBAC (user-access, vault allowlist,
// global), signed routes, and credential/scope/role management over ws and the CLI.

#include "support/s3_gateway_fixture.hpp"

namespace vh::test::s3_gateway {

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

TEST_F(S3GatewayDbTest, UserAccessCredentialUsesPrincipalRbacWithoutGatewayRoleAssignment) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto credential = createCredential(admin->id, "user_access");

    const auto decision = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject);

    EXPECT_TRUE(decision.allowed);
    EXPECT_TRUE(decision.principal_allowed);
    EXPECT_TRUE(decision.credential_allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::Allowed, decision.reason);
    EXPECT_TRUE(vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id).empty());
    EXPECT_FALSE(vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id));
    EXPECT_TRUE(vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential.id).empty());
}

TEST_F(S3GatewayDbTest, UserAccessCredentialCannotExceedPrincipalRbac) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    const auto unownedVaultId = createLocalVault(s3GatewayUniqueSuffix("user_access_denied"), admin->id);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = unownedVaultId,
        .bucket_name = "binding-no-access-" + std::to_string(unownedVaultId),
        .api_exclusive = true,
        .mode = "local",
        .created_by = admin->id
    });
    const auto principal = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(principal);
    ASSERT_FALSE(principal->isAdmin());
    const auto credential = createCredential(principal->id, "user_access");

    const auto decision = evaluateS3(
        principal,
        credential.id,
        credential.scope_mode,
        unownedVaultId,
        vh::rbac::s3::policy::S3Action::GetObject);

    EXPECT_FALSE(decision.allowed);
    EXPECT_FALSE(decision.principal_allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::PrincipalRbacDenied, decision.reason);
}

TEST_F(S3GatewayDbTest, VaultAllowlistCredentialRequiresSelectedVaultAndDefaultRole) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto credential = createCredential(admin->id, "vault_allowlist");

    const auto unselected = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject);

    EXPECT_FALSE(unselected.allowed);
    EXPECT_TRUE(unselected.principal_allowed);
    EXPECT_FALSE(unselected.credential_allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::VaultNotSelected, unselected.reason);

    selectCredentialVault(credential.id, vaultId, admin->id);

    const auto missingDefault = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject);

    EXPECT_FALSE(missingDefault.allowed);
    EXPECT_TRUE(missingDefault.principal_allowed);
    EXPECT_FALSE(missingDefault.credential_allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::MissingDefaultRole, missingDefault.reason);
}

TEST_F(S3GatewayDbTest, VaultAllowlistDefaultRoleRequiresPrincipalAndCredentialAllow) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    auto credential = createCredential(admin->id, "vault_allowlist");
    setCredentialDefaultVaultRole(credential.id, "reader", admin->id);
    selectCredentialVault(credential.id, vaultId, admin->id);

    auto read = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject);
    EXPECT_TRUE(read.allowed);
    EXPECT_TRUE(vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id).empty());

    auto write = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::PutObject,
        "/new-object.txt",
        false);
    EXPECT_FALSE(write.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::EffectiveCredentialRoleDenied, write.reason);

    const auto adminOwnedVaultId = createLocalVault(s3GatewayUniqueSuffix("allowlist_principal_denied"), admin->id);
    const auto principal = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(principal);
    credential = createCredential(principal->id, "vault_allowlist");
    setCredentialDefaultVaultRole(credential.id, "manager", principal->id);
    selectCredentialVault(credential.id, adminOwnedVaultId, principal->id);

    auto principalDenied = evaluateS3(
        principal,
        credential.id,
        credential.scope_mode,
        adminOwnedVaultId,
        vh::rbac::s3::policy::S3Action::GetObject);
    EXPECT_FALSE(principalDenied.allowed);
    EXPECT_FALSE(principalDenied.principal_allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::PrincipalRbacDenied, principalDenied.reason);
}

TEST_F(S3GatewayDbTest, VaultAllowlistPerVaultRoleOverridesDefaultRoleForOneVault) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto secondVaultId = createLocalVault(s3GatewayUniqueSuffix("allowlist_role_override"), admin->id);
    const auto credential = createCredential(admin->id, "vault_allowlist");
    setCredentialDefaultVaultRole(credential.id, "reader", admin->id);
    selectCredentialVault(credential.id, vaultId, admin->id);
    selectCredentialVault(credential.id, secondVaultId, admin->id);
    assignCredentialVaultRole(credential.id, secondVaultId, "contributor", admin->id);

    const auto firstWrite = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::PutObject,
        "/first.txt",
        false);
    EXPECT_FALSE(firstWrite.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::EffectiveCredentialRoleDenied, firstWrite.reason);

    const auto secondWrite = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        secondVaultId,
        vh::rbac::s3::policy::S3Action::PutObject,
        "/second.txt",
        false);
    EXPECT_TRUE(secondWrite.allowed);
}

TEST_F(S3GatewayDbTest, VaultAllowlistRoleOverridesApplyToCredentialAperture) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto credential = createCredential(admin->id, "vault_allowlist");
    setCredentialDefaultVaultRole(credential.id, "reader", admin->id);
    selectCredentialVault(credential.id, vaultId, admin->id);
    vh::db::query::s3::Gateway::upsertCredentialDefaultVaultRoleOverride(
        credential.id,
        makeGatewayOverride(
            "vault.fs.files.download",
            "/private/**",
            vh::rbac::permission::OverrideOpt::DENY));

    auto publicRead = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject,
        "/public/report.txt");
    EXPECT_TRUE(publicRead.allowed);

    auto privateRead = evaluateS3(
        admin,
        credential.id,
        credential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject,
        "/private/report.txt");
    EXPECT_FALSE(privateRead.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::EffectiveCredentialRoleDenied, privateRead.reason);
    ASSERT_TRUE(privateRead.credential_decision);
    EXPECT_EQ(vh::rbac::fs::policy::Decision::Reason::DeniedByOverride,
              privateRead.credential_decision->reason);

    const auto secondVaultId = createLocalVault(s3GatewayUniqueSuffix("allowlist_override_narrow"), admin->id);
    const auto perVaultCredential = createCredential(admin->id, "vault_allowlist");
    setCredentialDefaultVaultRole(perVaultCredential.id, "reader", admin->id);
    selectCredentialVault(perVaultCredential.id, vaultId, admin->id);
    selectCredentialVault(perVaultCredential.id, secondVaultId, admin->id);
    assignCredentialVaultRole(perVaultCredential.id, secondVaultId, "reader", admin->id);
    vh::db::query::s3::Gateway::upsertCredentialVaultRoleOverride(
        perVaultCredential.id,
        secondVaultId,
        makeGatewayOverride(
            "vault.fs.files.download",
            "/private/**",
            vh::rbac::permission::OverrideOpt::DENY));

    auto firstVaultPrivateRead = evaluateS3(
        admin,
        perVaultCredential.id,
        perVaultCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject,
        "/private/report.txt");
    EXPECT_TRUE(firstVaultPrivateRead.allowed);

    auto secondVaultPrivateRead = evaluateS3(
        admin,
        perVaultCredential.id,
        perVaultCredential.scope_mode,
        secondVaultId,
        vh::rbac::s3::policy::S3Action::GetObject,
        "/private/report.txt");
    EXPECT_FALSE(secondVaultPrivateRead.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::EffectiveCredentialRoleDenied, secondVaultPrivateRead.reason);
    ASSERT_TRUE(secondVaultPrivateRead.credential_decision);
    EXPECT_EQ(vh::rbac::fs::policy::Decision::Reason::DeniedByOverride,
              secondVaultPrivateRead.credential_decision->reason);

    const auto allowCredential = createCredential(admin->id, "vault_allowlist");
    setCredentialDefaultVaultRole(allowCredential.id, "implicit_deny", admin->id);
    selectCredentialVault(allowCredential.id, vaultId, admin->id);
    assignCredentialVaultRole(allowCredential.id, vaultId, "implicit_deny", admin->id);
    vh::db::query::s3::Gateway::upsertCredentialVaultRoleOverride(
        allowCredential.id,
        vaultId,
        makeGatewayOverride(
            "vault.fs.files.download",
            "/allowed/**",
            vh::rbac::permission::OverrideOpt::ALLOW));

    auto allowedByOverride = evaluateS3(
        admin,
        allowCredential.id,
        allowCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject,
        "/allowed/report.txt");
    EXPECT_TRUE(allowedByOverride.allowed);
    ASSERT_TRUE(allowedByOverride.credential_decision);
    EXPECT_EQ(vh::rbac::fs::policy::Decision::Reason::AllowedByOverride,
              allowedByOverride.credential_decision->reason);
}

TEST_F(S3GatewayDbTest, GlobalCredentialUsesPrincipalRbacAndRequiresAdminPrincipal) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto globalAdminCredential = createCredential(admin->id, "global");
    const auto secondVaultId = createLocalVault(s3GatewayUniqueSuffix("global_default"), admin->id);

    auto missingDefault = evaluateS3(
        admin,
        globalAdminCredential.id,
        globalAdminCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject);
    EXPECT_FALSE(missingDefault.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::MissingDefaultRole, missingDefault.reason);

    setCredentialDefaultVaultRole(globalAdminCredential.id, "reader", admin->id);

    auto adminDecision = evaluateS3(
        admin,
        globalAdminCredential.id,
        globalAdminCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::GetObject);
    EXPECT_TRUE(adminDecision.allowed);

    auto secondVaultRead = evaluateS3(
        admin,
        globalAdminCredential.id,
        globalAdminCredential.scope_mode,
        secondVaultId,
        vh::rbac::s3::policy::S3Action::GetObject);
    EXPECT_TRUE(secondVaultRead.allowed);

    auto adminWriteDenied = evaluateS3(
        admin,
        globalAdminCredential.id,
        globalAdminCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::PutObject,
        "/global-write.txt",
        false);
    EXPECT_FALSE(adminWriteDenied.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::EffectiveCredentialRoleDenied, adminWriteDenied.reason);

    assignCredentialVaultRole(globalAdminCredential.id, vaultId, "manager", admin->id);
    auto adminWriteAllowedByException = evaluateS3(
        admin,
        globalAdminCredential.id,
        globalAdminCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::PutObject,
        "/global-write.txt",
        false);
    EXPECT_TRUE(adminWriteAllowedByException.allowed);

    assignPrincipalVaultRole(vaultId, userId, "manager");
    const auto nonAdmin = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(nonAdmin);
    ASSERT_FALSE(nonAdmin->isAdmin());
    const auto globalUserCredential = createCredential(nonAdmin->id, "global");
    setCredentialDefaultVaultRole(globalUserCredential.id, "manager", nonAdmin->id);

    auto userDecision = evaluateS3(
        nonAdmin,
        globalUserCredential.id,
        globalUserCredential.scope_mode,
        vaultId,
        vh::rbac::s3::policy::S3Action::PutObject,
        "/new-global-object.txt",
        false);
    EXPECT_FALSE(userDecision.allowed);
    EXPECT_TRUE(userDecision.principal_allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::GlobalPrincipalRequired, userDecision.reason);
}

TEST_F(S3GatewayDbTest, ManagementS3ActionsRequireExplicitManagementGates) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    const auto decision = evaluateS3(
        admin,
        0,
        "user_access",
        vaultId,
        vh::rbac::s3::policy::S3Action::ManageCredential);

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(vh::rbac::s3::policy::Decision::Reason::NoFilesystemMapping, decision.reason);
}

TEST_F(S3GatewayDbTest, VaultAllowlistScopesMirrorToGatewayVaultRoles) {
    auto admin = std::make_shared<vh::identities::User>();
    admin->id = userId;
    admin->name = "admin-principal";
    admin->roles.admin = std::make_shared<vh::rbac::role::Admin>(
        vh::rbac::role::Admin::SuperAdmin(admin->id));

    vh::db::query::s3::GatewayCredential credential;
    credential.user_id = admin->id;
    credential.principal_user_id = admin->id;
    credential.created_by = admin->id;
    credential.name = "scoped-admin-test";
    credential.access_key = "VHTESTSCOPEDADMIN";
    credential.encrypted_secret_access_key = {1, 2, 3};
    credential.iv = {4, 5, 6};
    credential.enabled = true;
    credential.scope_mode = "vault_allowlist";
    credential.id = vh::db::query::s3::Gateway::createCredential(credential);

    vh::db::query::s3::Gateway::replaceCredentialScopeShorthand(credential.id, {{
        .credential_id = credential.id,
        .vault_id = vaultId,
        .can_list = true,
        .can_read = true,
        .can_write = true,
        .can_delete = true,
        .can_admin = false
    }});

    auto assignments = vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    EXPECT_TRUE(assignments.empty());
    auto defaultRole = vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    ASSERT_TRUE(defaultRole);
    EXPECT_EQ(defaultRole->vault_role_id, roleIdByName("manager"));
    auto selectedVaults = vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential.id);
    ASSERT_EQ(selectedVaults.size(), 1u);
    EXPECT_EQ(selectedVaults.front().vault_id, vaultId);
    auto role = vh::db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, vaultId);
    EXPECT_FALSE(role);
    auto effectiveRole = vh::db::query::s3::Gateway::getEffectiveCredentialVaultRole(credential.id, vaultId, credential.scope_mode);
    ASSERT_TRUE(effectiveRole);
    EXPECT_EQ(effectiveRole->name, "manager");

    vh::db::query::s3::Gateway::replaceCredentialScopeShorthand(credential.id, {{
        .credential_id = credential.id,
        .vault_id = vaultId,
        .can_list = true,
        .can_read = true,
        .can_write = false,
        .can_delete = true,
        .can_admin = false
    }});

    assignments = vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    EXPECT_TRUE(assignments.empty());
    defaultRole = vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    ASSERT_TRUE(defaultRole);
    EXPECT_EQ(defaultRole->vault_role_id, roleIdByName("manager"));
    role = vh::db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, vaultId);
    EXPECT_FALSE(role);
    effectiveRole = vh::db::query::s3::Gateway::getEffectiveCredentialVaultRole(credential.id, vaultId, credential.scope_mode);
    ASSERT_TRUE(effectiveRole);
    EXPECT_EQ(effectiveRole->name, "manager");

    vh::db::query::s3::Gateway::replaceCredentialScopeShorthand(credential.id, {{
        .credential_id = credential.id,
        .vault_id = vaultId,
        .can_list = true,
        .can_read = true,
        .can_write = false,
        .can_delete = false,
        .can_admin = false
    }});

    defaultRole = vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id);
    ASSERT_TRUE(defaultRole);
    EXPECT_EQ(defaultRole->vault_role_id, roleIdByName("reader"));
    assignments = vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id);
    EXPECT_TRUE(assignments.empty());
    role = vh::db::query::s3::Gateway::getCredentialVaultRoleForVault(credential.id, vaultId);
    EXPECT_FALSE(role);
    effectiveRole = vh::db::query::s3::Gateway::getEffectiveCredentialVaultRole(credential.id, vaultId, credential.scope_mode);
    ASSERT_TRUE(effectiveRole);
    EXPECT_EQ(effectiveRole->name, "reader");
}

TEST_F(S3GatewayDbTest, SignedDeleteBucketUsesCanAdminWithoutCanDeleteScope) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.require_sigv4 = true;
    cfg.s3_gateway.allow_path_style = true;
    vh::config::Registry::set(cfg);

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const std::string bucketName = "admin-delete-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = false,
        .mode = "local",
        .created_by = admin->id
    });

    const vh::protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential({
        .created_by = admin->id,
        .principal_user_id = admin->id,
        .name = "route-admin-delete-" + s3GatewayUniqueSuffix("credential"),
        .scope_mode = "vault_allowlist",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {{
            .credential_id = 0,
            .vault_id = vaultId,
            .can_list = false,
            .can_read = false,
            .can_write = false,
            .can_delete = false,
            .can_admin = true
        }}
    });

    vh::protocols::s3::Router::Request request{http::verb::delete_, "/" + bucketName, 11};
    request.set(http::field::host, "localhost:39000");
    signS3GatewayRequest(request, secret.credential.access_key, secret.secret_access_key);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::no_content) << response.body();
    EXPECT_FALSE(vh::db::query::s3::Gateway::resolveBucket(bucketName));
}

TEST_F(S3GatewayDbTest, SignedDedicatedHostRootListsBuckets) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.require_sigv4 = true;
    cfg.s3_gateway.allow_path_style = true;
    cfg.s3_gateway.allow_virtual_hosted_style = true;
    vh::config::Registry::set(cfg);

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);

    const vh::protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential({
        .created_by = admin->id,
        .principal_user_id = admin->id,
        .name = "dedicated-root-" + s3GatewayUniqueSuffix("credential"),
        .scope_mode = "user_access",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {}
    });

    vh::protocols::s3::Router::Request request{http::verb::get, "/", 11};
    request.set(http::field::host, "s3.vaulthalla.dev");
    request.set("X-Vaulthalla-S3-Path-Style-Only", "true");
    signS3GatewayRequest(request, secret.credential.access_key, secret.secret_access_key);

    const vh::protocols::s3::Router router;
    const auto response = router.route(std::move(request));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_NE(response.body().find("<ListAllMyBucketsResult"), std::string::npos);
}

TEST_F(S3GatewayDbTest, SignedDedicatedHostPutAndGetAuthenticateAndRoutePathStyle) {
    ConfigRestore restoreConfig(vh::config::Registry::get());

    auto cfg = vh::config::Registry::get();
    cfg.s3_gateway.require_sigv4 = true;
    cfg.s3_gateway.allow_path_style = true;
    cfg.s3_gateway.allow_virtual_hosted_style = true;
    vh::config::Registry::set(cfg);

    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());

    const std::string bucketName = "dedicated-route-" + std::to_string(vaultId);
    vh::db::query::s3::Gateway::bindBucket({
        .vault_id = vaultId,
        .bucket_name = bucketName,
        .api_exclusive = true,
        .mode = "local",
        .created_by = admin->id
    });

    const vh::protocols::s3::CredentialManager manager;
    auto secret = manager.createCredential({
        .created_by = admin->id,
        .principal_user_id = admin->id,
        .name = "dedicated-object-" + s3GatewayUniqueSuffix("credential"),
        .scope_mode = "vault_allowlist",
        .description = std::nullopt,
        .expires_at = std::nullopt,
        .vault_scopes = {{
            .credential_id = 0,
            .vault_id = vaultId,
            .can_list = true,
            .can_read = true,
            .can_write = true,
            .can_delete = false,
            .can_admin = false
        }}
    });

    const auto addDedicatedHostHeaders = [](vh::protocols::s3::Router::Request& request) {
        request.set(http::field::host, "s3.vaulthalla.dev");
        request.set("X-Vaulthalla-S3-Path-Style-Only", "true");
    };

    vh::protocols::s3::Router::Request put{http::verb::put, "/" + bucketName + "/key.txt", 11};
    put.body() = "dedicated host body";
    put.prepare_payload();
    addDedicatedHostHeaders(put);
    signS3GatewayRequest(put, secret.credential.access_key, secret.secret_access_key);

    const vh::protocols::s3::Router router;
    auto response = router.route(std::move(put));
    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_TRUE(vh::db::query::s3::Gateway::getObjectState(vaultId, "key.txt"));

    vh::protocols::s3::Router::Request get{http::verb::get, "/" + bucketName + "/key.txt", 11};
    addDedicatedHostHeaders(get);
    signS3GatewayRequest(get, secret.credential.access_key, secret.secret_access_key);
    response = router.route(std::move(get));

    EXPECT_EQ(response.result(), http::status::ok) << response.body();
    EXPECT_EQ(response.body(), "dedicated host body");
}

// The scope rule moved from CredentialManager::validateScopeMutation into ops::s3_gateway, the one place both
// surfaces authorize credential policy.
TEST_F(S3GatewayDbTest, NonAdminScopeMutationCannotGrantGatewayAdminScope) {
    const auto user = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(user);
    EXPECT_THROW(
        (void)vh::ops::s3_gateway::createCredential(user, {
            .name = "non-admin-admin-scope-" + s3GatewayUniqueSuffix("credential"),
            .scope_mode = std::string{"vault_allowlist"},
            .vault_access = {{
                .vault_id = vaultId,
                .list = true,
                .read = true,
                .write = false,
                .del = false,
                .admin = true
            }}}),
        vh::ops::Error);
}

TEST_F(S3GatewayDbTest, NonAdminScopeMutationCannotNameUnownedVaultEvenWithNoActions) {
    const auto unownedVaultId = vh::db::Transactions::exec(
        "S3GatewayDbTest::seedUnownedScopeVault",
        [](pqxx::work& txn) {
            const auto admin = vh::db::query::identities::User::getUserByName("admin");
            if (!admin) throw std::runtime_error("admin user not available");
            const auto mount = s3GatewayUniqueSuffix("s3gw_unscope").substr(0, 33);
            const auto seededVaultId = txn.exec(
                "INSERT INTO vault (type, name, owner_id, mount_point, description) VALUES ($1, $2, $3, $4, $5) RETURNING id",
                pqxx::params{"local", "S3 Gateway Unowned Scope Vault", admin->id, mount, ""}
            ).one_field().as<uint32_t>();
            txn.exec(
                "WITH ins AS (INSERT INTO sync (vault_id, interval) VALUES ($1, 300) RETURNING id) "
                "INSERT INTO fsync (sync_id, conflict_policy) SELECT id, 'keep_both' FROM ins",
                pqxx::params{seededVaultId});
            return seededVaultId;
        });

    const auto user = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(user);
    EXPECT_THROW(
        (void)vh::ops::s3_gateway::createCredential(user, {
            .name = "non-admin-unowned-" + s3GatewayUniqueSuffix("credential"),
            .scope_mode = std::string{"vault_allowlist"},
            .vault_access = {{
                .vault_id = unownedVaultId,
                .list = false,
                .read = false,
                .write = false,
                .del = false,
                .admin = false
            }}}),
        vh::ops::Error);
}

TEST_F(S3GatewayDbTest, UserAccessCredentialCannotCreateBucketWithoutAdminPrincipal) {
    auto user = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(user);
    ASSERT_FALSE(user->isAdmin());

    vh::protocols::s3::AuthContext auth{
        .user = user,
        .credential = {},
        .credential_id = 123,
        .access_key = "VHTESTUSERACCESSCREATE",
        .scope_mode = "user_access",
        .dev_context = false
    };

    const vh::protocols::s3::ObjectStore store;
    EXPECT_THROW((void)store.createBucket("user-access-create-denied", auth), vh::protocols::s3::S3Error);
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketListHandlersAcceptNullPayloads) {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    session->user = vh::db::query::identities::User::getUserById(userId);
    ASSERT_TRUE(session->user);

    const auto credentials = vh::protocols::ws::handler::S3Gateway::credentialsList(nlohmann::json(nullptr), session);
    ASSERT_TRUE(credentials.contains("credentials"));
    EXPECT_TRUE(credentials.at("credentials").is_array());

    const auto policies = vh::protocols::ws::handler::S3Gateway::budgetPolicyList(nlohmann::json(nullptr), session);
    ASSERT_TRUE(policies.contains("policies"));
    EXPECT_TRUE(policies.at("policies").is_array());
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketNormalizesCredentialScopeNames) {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    session->user = vh::db::query::identities::User::getUserById(1);
    ASSERT_TRUE(session->user);
    ASSERT_TRUE(session->user->isAdmin());

    const auto created = vh::protocols::ws::handler::S3Gateway::credentialsCreate({
        {"name", "ws-normalized-scope-" + s3GatewayUniqueSuffix("credential")},
        {"scope_mode", "vault-allowlist"},
        {"vault_scopes", nlohmann::json::array({
            {
                {"vault_id", vaultId},
                {"can_list", true},
                {"can_read", true},
                {"can_write", false},
                {"can_delete", false},
                {"can_admin", false}
            }
        })}
    }, session);
    ASSERT_TRUE(created.contains("credential"));
    EXPECT_EQ("vault_allowlist", created.at("credential").at("scope_mode").get<std::string>());
    const auto accessKey = created.at("credential").at("access_key").get<std::string>();
    const auto credential = vh::db::query::s3::Gateway::getCredentialByAccessKey(accessKey);
    ASSERT_TRUE(credential);
    EXPECT_EQ("vault_allowlist", credential->scope_mode);
    EXPECT_EQ(1u, vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential->id).size());
    EXPECT_TRUE(vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id));

    const auto updated = vh::protocols::ws::handler::S3Gateway::credentialsScopeUpdate({
        {"access_key", accessKey},
        {"scope_mode", "user-access"}
    }, session);
    ASSERT_TRUE(updated.contains("credential"));
    EXPECT_EQ("user_access", updated.at("credential").at("scope_mode").get<std::string>());
    EXPECT_TRUE(vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential->id).empty());
    EXPECT_FALSE(vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential->id));
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketRoleAssignmentAndOverrideEndpointsWork) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto session = wsSessionForUser(admin->id);
    const auto credential = createCredential(admin->id, "user_access");

    const auto assigned = vh::protocols::ws::handler::S3Gateway::credentialsRolesAssign({
        {"credential_id", credential.id},
        {"vault_id", vaultId},
        {"vault_role_name", "reader"},
        {"enabled", true}
    }, session);
    ASSERT_TRUE(assigned.contains("assignment"));
    EXPECT_EQ(credential.id, assigned.at("assignment").at("credential_id").get<uint32_t>());
    EXPECT_EQ(vaultId, assigned.at("assignment").at("vault_id").get<uint32_t>());
    EXPECT_EQ("reader", assigned.at("assignment").at("role").at("name").get<std::string>());

    const auto updatedCredential = vh::db::query::s3::Gateway::getCredentialByAccessKey(credential.access_key);
    ASSERT_TRUE(updatedCredential);
    EXPECT_EQ("vault_allowlist", updatedCredential->scope_mode);

    const auto listed = vh::protocols::ws::handler::S3Gateway::credentialsRolesList({
        {"credential_id", credential.id}
    }, session);
    ASSERT_TRUE(listed.contains("roles"));
    ASSERT_EQ(1u, listed.at("roles").size());
    EXPECT_TRUE(listed.at("roles").front().contains("vault"));

    const auto addedOverride = vh::protocols::ws::handler::S3Gateway::credentialsRoleOverridesAdd({
        {"credential_id", credential.id},
        {"vault_name", "S3 Gateway Test Vault"},
        {"permission_qualified", "vault.fs.files.download"},
        {"glob_path", "/private/**"},
        {"effect", "deny"},
        {"enabled", true}
    }, session);
    ASSERT_TRUE(addedOverride.contains("override"));
    const auto overrideId = addedOverride.at("override").at("id").get<uint32_t>();
    EXPECT_EQ("vault.fs.files.download", addedOverride.at("override").at("permission_qualified").get<std::string>());
    EXPECT_EQ("/private/**", addedOverride.at("override").at("glob_path").get<std::string>());

    const auto overrides = vh::protocols::ws::handler::S3Gateway::credentialsRoleOverridesList({
        {"credential_id", credential.id},
        {"vault_id", vaultId}
    }, session);
    ASSERT_TRUE(overrides.contains("overrides"));
    ASSERT_EQ(1u, overrides.at("overrides").size());
    EXPECT_EQ(overrideId, overrides.at("overrides").front().at("id").get<uint32_t>());

    const auto removedOverride = vh::protocols::ws::handler::S3Gateway::credentialsRoleOverridesRemove({
        {"credential_id", credential.id},
        {"vault_id", vaultId},
        {"override_id", overrideId}
    }, session);
    EXPECT_TRUE(removedOverride.at("removed").get<bool>());

    const auto revoked = vh::protocols::ws::handler::S3Gateway::credentialsRolesRevoke({
        {"credential_id", credential.id},
        {"vault_id", vaultId}
    }, session);
    EXPECT_TRUE(revoked.at("revoked").get<bool>());
    EXPECT_TRUE(vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id).empty());
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketDefaultRoleSelectedVaultAndDefaultOverrideEndpointsWork) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto session = wsSessionForUser(admin->id);
    const auto secondVaultId = createLocalVault(s3GatewayUniqueSuffix("ws_selected"), admin->id);
    const auto credential = createCredential(admin->id, "vault_allowlist");

    const auto initialDefault = vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleGet({
        {"credential_id", credential.id}
    }, session);
    ASSERT_TRUE(initialDefault.contains("default_role"));
    EXPECT_TRUE(initialDefault.at("default_role").is_null());

    const auto setDefault = vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleSet({
        {"credential_id", credential.id},
        {"vault_role_name", "reader"},
        {"enabled", true}
    }, session);
    ASSERT_TRUE(setDefault.contains("default_role"));
    EXPECT_EQ("reader", setDefault.at("default_role").at("role").at("name").get<std::string>());

    const auto addedSelected = vh::protocols::ws::handler::S3Gateway::credentialsSelectedVaultsAdd({
        {"credential_id", credential.id},
        {"vault_id", vaultId},
        {"enabled", true}
    }, session);
    ASSERT_TRUE(addedSelected.contains("selected_vault"));
    EXPECT_EQ(vaultId, addedSelected.at("selected_vault").at("vault_id").get<uint32_t>());
    EXPECT_TRUE(addedSelected.at("selected_vault").contains("vault"));

    const auto replacedSelected = vh::protocols::ws::handler::S3Gateway::credentialsSelectedVaultsReplace({
        {"credential_id", credential.id},
        {"selected_vault_ids", {vaultId, secondVaultId}}
    }, session);
    ASSERT_TRUE(replacedSelected.contains("selected_vaults"));
    EXPECT_EQ(2u, replacedSelected.at("selected_vaults").size());

    const auto listedSelected = vh::protocols::ws::handler::S3Gateway::credentialsSelectedVaultsList({
        {"credential_id", credential.id}
    }, session);
    ASSERT_TRUE(listedSelected.contains("selected_vaults"));
    EXPECT_EQ(2u, listedSelected.at("selected_vaults").size());

    const auto removedSelected = vh::protocols::ws::handler::S3Gateway::credentialsSelectedVaultsRemove({
        {"credential_id", credential.id},
        {"vault_id", secondVaultId}
    }, session);
    EXPECT_TRUE(removedSelected.at("removed").get<bool>());

    const auto addedOverride = vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleOverridesAdd({
        {"credential_id", credential.id},
        {"permission_qualified", "vault.fs.files.download"},
        {"glob_path", "/shared/**"},
        {"effect", "deny"},
        {"enabled", true}
    }, session);
    ASSERT_TRUE(addedOverride.contains("override"));
    const auto overrideId = addedOverride.at("override").at("id").get<uint32_t>();
    EXPECT_EQ("vault.fs.files.download", addedOverride.at("override").at("permission_qualified").get<std::string>());
    EXPECT_EQ("/shared/**", addedOverride.at("override").at("glob_path").get<std::string>());

    const auto listedOverrides = vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleOverridesList({
        {"credential_id", credential.id}
    }, session);
    ASSERT_TRUE(listedOverrides.contains("overrides"));
    ASSERT_EQ(1u, listedOverrides.at("overrides").size());
    EXPECT_EQ(overrideId, listedOverrides.at("overrides").front().at("id").get<uint32_t>());

    const auto removedOverride = vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleOverridesRemove({
        {"credential_id", credential.id},
        {"override_id", overrideId}
    }, session);
    EXPECT_TRUE(removedOverride.at("removed").get<bool>());

    const auto clearedDefault = vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleClear({
        {"credential_id", credential.id}
    }, session);
    EXPECT_TRUE(clearedDefault.at("cleared").get<bool>());
    EXPECT_FALSE(vh::db::query::s3::Gateway::getCredentialDefaultVaultRole(credential.id));
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketDefaultPolicyMutationRequiresCredentialAndVaultAuthority) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    ASSERT_TRUE(admin->isSuperAdmin());
    const auto adminCredential = createCredential(admin->id, "vault_allowlist");
    const auto actorSession = wsSessionForUser(userId);

    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleSet({
            {"credential_id", adminCredential.id},
            {"vault_role_name", "reader"}
        }, actorSession),
        std::exception);

    const auto manageUserId = userWithAdminRole(
        "selected_no_vault",
        adminRoleWithS3("selected_no_vault", vh::rbac::permission::admin::S3Gateway::CredentialManager()));
    const auto manageSession = wsSessionForUser(manageUserId);
    const auto manageCredential = createCredential(manageUserId, "vault_allowlist");
    (void)vh::protocols::ws::handler::S3Gateway::credentialsDefaultRoleSet({
        {"credential_id", manageCredential.id},
        {"vault_role_name", "reader"}
    }, manageSession);

    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::credentialsSelectedVaultsAdd({
            {"credential_id", manageCredential.id},
            {"vault_id", vaultId}
        }, manageSession),
        std::exception);
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketPermissionGatesUseGatewayAdminPermissions) {
    const auto admin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(admin);
    const auto adminCredential = createCredential(admin->id, "user_access");

    const auto noGatewayUserId = userWithAdminRole(
        "no_gateway",
        adminRoleWithS3("none", vh::rbac::permission::admin::S3Gateway::None()));
    const auto noGatewaySession = wsSessionForUser(noGatewayUserId);
    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::status(nlohmann::json(nullptr), noGatewaySession),
        std::exception);

    const auto viewUserId = userWithAdminRole(
        "view",
        adminRoleWithS3("view", vh::rbac::permission::admin::S3Gateway::ViewOnly()));
    const auto viewSession = wsSessionForUser(viewUserId);
    EXPECT_NO_THROW((void)vh::protocols::ws::handler::S3Gateway::status(nlohmann::json(nullptr), viewSession));
    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::credentialsRevoke({
            {"access_key", adminCredential.access_key}
        }, viewSession),
        std::exception);

    const auto manageUserId = userWithAdminRole(
        "manage_credentials",
        adminRoleWithS3("manage_credentials", vh::rbac::permission::admin::S3Gateway::CredentialManager()));
    const auto manageSession = wsSessionForUser(manageUserId);
    const auto manageCredential = createCredential(manageUserId, "user_access");
    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::credentialsScopeUpdate({
            {"credential_id", manageCredential.id},
            {"scope_mode", "user_access"},
            {"principal_user_id", userId}
        }, manageSession),
        std::exception);

    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::bucketsBind({
            {"bucket_name", "gate-bind-" + std::to_string(vaultId)},
            {"vault_id", vaultId}
        }, manageSession),
        std::exception);

    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
            {"scope", "gateway_credential"},
            {"gateway_credential_id", manageCredential.id},
            {"mode", "enforce"},
            {"max_daily_cost", "1.00"},
            {"currency", "USD"}
        }, manageSession),
        std::exception);

    EXPECT_THROW(
        (void)vh::protocols::ws::handler::S3Gateway::credentialsRolesAssign({
            {"credential_id", manageCredential.id},
            {"vault_id", vaultId},
            {"vault_role_name", "reader"}
        }, manageSession),
        std::exception);
}

// Stage 0 S4: `vh s3-gateway creds scope <cred> revoke-vault <vault>` used to delete the credential's vault role
// assignment and selected vault after only "principal or ManageCredentials", with no vault-role Revoke check. The ws
// twin (s3.gateway.credentials.selectedVaults.remove) always required RolePermissions::Revoke on that vault.
TEST_F(S3GatewayDbTest, CliScopeRevokeVaultRequiresVaultRoleRevoke) {
    if (!vh::runtime::Deps::get().shellUsageManager)
        vh::runtime::Deps::get().shellUsageManager = std::make_shared<vh::protocols::shell::UsageManager>();
    const auto router = std::make_shared<vh::protocols::shell::Router>();
    vh::protocols::shell::commands::registerS3GatewayCommands(router);

    const auto principalId = userWithAdminRole(
        "revoke_principal",
        adminRoleWithS3("revoke_principal", vh::rbac::permission::admin::S3Gateway::None()));
    assignPrincipalVaultRole(vaultId, principalId, "reader");
    const auto credential = createCredential(principalId, "vault_allowlist");
    setCredentialDefaultVaultRole(credential.id, "reader");
    selectCredentialVault(credential.id, vaultId);
    assignCredentialVaultRole(credential.id, vaultId, "reader");

    const auto selectedCount = [&] {
        return vh::db::query::s3::Gateway::listCredentialSelectedVaults(credential.id).size();
    };
    const auto assignmentCount = [&] {
        return vh::db::query::s3::Gateway::listCredentialVaultRoleAssignments(credential.id).size();
    };
    ASSERT_EQ(1u, selectedCount());
    ASSERT_EQ(1u, assignmentCount());

    const auto line = "s3-gateway creds scope " + std::to_string(credential.id) + " revoke-vault " + std::to_string(vaultId);
    const auto run = [&](const uint32_t callerId) {
        const auto caller = vh::db::query::identities::User::getUserById(callerId);
        try {
            return router->executeLine(line, caller, nullptr).exit_code;
        } catch (const std::exception&) {
            return 1;
        }
    };

    // Credential manager that may retarget principals but holds no vault-role rights on this vault.
    const auto managerId = userWithAdminRole(
        "revoke_manager",
        adminRoleWithS3("revoke_manager", vh::rbac::permission::admin::S3Gateway::PrincipalAssigner()));
    EXPECT_NE(0, run(managerId));
    EXPECT_EQ(1u, selectedCount()) << "revoke-vault removed the selected vault without vault-role Revoke";
    EXPECT_EQ(1u, assignmentCount()) << "revoke-vault removed the role assignment without vault-role Revoke";

    // Positive control: a caller with vault-role authority (super_admin) still revokes through the same line.
    const auto superAdmin = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(superAdmin);
    ASSERT_TRUE(superAdmin->isSuperAdmin());
    EXPECT_EQ(0, run(superAdmin->id));
    EXPECT_EQ(0u, selectedCount());
    EXPECT_EQ(0u, assignmentCount());
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketAssignPrincipalPermissionAllowsRetargeting) {
    const auto assignerUserId = userWithAdminRole(
        "assigner",
        adminRoleWithS3("assigner", vh::rbac::permission::admin::S3Gateway::PrincipalAssigner()));
    const auto assignerSession = wsSessionForUser(assignerUserId);
    const auto credential = createCredential(assignerUserId, "user_access");

    const auto updated = vh::protocols::ws::handler::S3Gateway::credentialsScopeUpdate({
        {"credential_id", credential.id},
        {"scope_mode", "user_access"},
        {"principal_user_id", userId}
    }, assignerSession);

    ASSERT_TRUE(updated.contains("credential"));
    EXPECT_EQ(userId, updated.at("credential").at("principal_user_id").get<uint32_t>());
}

TEST_F(S3GatewayDbTest, S3GatewayWebSocketRejectsRemoteModeForLocalVaultBinding) {
    auto session = std::make_shared<vh::protocols::ws::Session>(
        std::make_shared<vh::protocols::ws::Router>());
    session->user = vh::db::query::identities::User::getUserByName("admin");
    ASSERT_TRUE(session->user);
    ASSERT_TRUE(session->user->isSuperAdmin());

    EXPECT_THROW(
        vh::protocols::ws::handler::S3Gateway::bucketsBind({
            {"bucket_name", "local-as-remote-" + std::to_string(vaultId)},
            {"vault_id", vaultId},
            {"mode", "remote_cache"}
        }, session),
        std::exception);

    const auto bound = vh::protocols::ws::handler::S3Gateway::bucketsBind({
        {"bucket_name", "local-binding-" + std::to_string(vaultId)},
        {"vault_id", vaultId}
    }, session);
    EXPECT_TRUE(bound.at("bound").get<bool>());
    const auto binding = vh::db::query::s3::Gateway::resolveBucket("local-binding-" + std::to_string(vaultId));
    ASSERT_TRUE(binding);
    EXPECT_EQ("local", binding->mode);
}

} // namespace vh::test::s3_gateway
