// S3CostSafetyTest, part 2/4: price-budget reservations, gateway credential/vault/provider budget policies,
// and budget management through ws (s3.gateway.*, pricing.budget.status) and the `vh s3` CLI.

#include "support/s3_cost_safety_fixture.hpp"

namespace vh::test::s3_cost_safety {

TEST(S3CostSafetyTest, PriceBudgetDryRunDoesNotCreateReservations) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed price budget reservation test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("budget_dry_run"));
    saveVaultBudgetPolicyForDbTest(
        vaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "10.00000000");

    const auto runUuid = uniqueSuffix("dry-run");
    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = true,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });

    EXPECT_TRUE(decision.allowed);
    EXPECT_TRUE(decision.reservations.empty());
    EXPECT_EQ(0u, countPriceBudgetLedgerForRunDbTest(runUuid));
}

TEST(S3CostSafetyTest, PriceBudgetBlockedBeforeExecuteDoesNotReserveSpend) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed price budget reservation test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("budget_blocked"));
    saveVaultBudgetPolicyForDbTest(
        vaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000");

    const auto runUuid = uniqueSuffix("blocked-run");
    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });

    EXPECT_FALSE(decision.allowed);
    EXPECT_TRUE(decision.stalled);
    EXPECT_TRUE(decision.reservations.empty());
    EXPECT_EQ(0u, countPriceBudgetLedgerForRunDbTest(runUuid));
}

TEST(S3CostSafetyTest, PriceBudgetWarnPolicyWithUnverifiedCatalogDoesNotReserveSpend) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed price budget reservation test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("budget_unverified_warn"));
    saveVaultBudgetPolicyForDbTest(
        vaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Warn,
        "10.00000000");

    const auto runUuid = uniqueSuffix("unverified-warn");
    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000", false),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });

    EXPECT_TRUE(decision.allowed);
    ASSERT_FALSE(decision.warnings.empty());
    EXPECT_TRUE(decision.reservations.empty());
    EXPECT_EQ(0u, countPriceBudgetLedgerForRunDbTest(runUuid));
}

TEST(S3CostSafetyTest, PriceBudgetReservationsConstrainSubsequentSharedPolicyChecks) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed price budget concurrency test due to missing environment variables.";
    ensureDbReady();

    const auto vaultId = seedS3VaultForDbTest(uniqueSuffix("budget_shared"));
    saveVaultBudgetPolicyForDbTest(
        vaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "1.00000000");

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto first = service.preflight({
        .vault_id = vaultId,
        .run_uuid = uniqueSuffix("shared-a"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });
    ASSERT_TRUE(first.allowed);
    ASSERT_FALSE(first.reservations.empty());

    const auto second = service.preflight({
        .vault_id = vaultId,
        .run_uuid = uniqueSuffix("shared-b"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });

    EXPECT_FALSE(second.allowed);
    EXPECT_TRUE(second.stalled);
    EXPECT_TRUE(second.reservations.empty());
}

TEST(S3CostSafetyTest, GatewayCredentialMonthlyBudgetAggregatesAcrossVaults) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway credential price budget test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_key_budget");
    const auto firstVaultId = seedS3VaultForDbTest(suffix + "_a");
    const auto secondVaultId = seedS3VaultForDbTest(suffix + "_b");
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(firstVaultId), suffix);
    const auto policy = saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        credentialId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "1.00000000");
    ASSERT_EQ(credentialId, *policy.gateway_credential_id);

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto firstRun = uniqueSuffix("gateway-key-a");
    const auto first = service.preflight({
        .vault_id = firstVaultId,
        .run_uuid = firstRun,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = firstRun,
        .operation = "PutObject",
        .object_key = "one.bin"
    });
    ASSERT_TRUE(first.allowed);
    ASSERT_FALSE(first.reservations.empty());

    const auto secondRun = uniqueSuffix("gateway-key-b");
    const auto second = service.preflight({
        .vault_id = secondVaultId,
        .run_uuid = secondRun,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = secondRun,
        .operation = "GetObject",
        .object_key = "two.bin"
    });

    EXPECT_FALSE(second.allowed);
    EXPECT_TRUE(second.stalled);
    EXPECT_TRUE(second.reservations.empty());

    const auto ledger = service.listLedger(10, firstVaultId, credentialId);
    ASSERT_FALSE(ledger.empty());
    EXPECT_EQ(credentialId, ledger.front().gateway_credential_id);
    EXPECT_EQ(firstVaultId, ledger.front().vault_id);
    ASSERT_TRUE(ledger.front().operation);
    EXPECT_EQ("PutObject", *ledger.front().operation);
    ASSERT_TRUE(ledger.front().object_key);
    EXPECT_EQ("one.bin", *ledger.front().object_key);
}

TEST(S3CostSafetyTest, GatewayCredentialVaultMonthlyBudgetCountsOnlyThatPair) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway credential/vault price budget test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_key_vault_budget");
    const auto firstVaultId = seedS3VaultForDbTest(suffix + "_a");
    const auto secondVaultId = seedS3VaultForDbTest(suffix + "_b");
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(firstVaultId), suffix);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credentialId,
        firstVaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "1.00000000");
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credentialId,
        secondVaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "1.00000000");

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto first = service.preflight({
        .vault_id = firstVaultId,
        .run_uuid = uniqueSuffix("gateway-key-vault-a"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = uniqueSuffix("gateway-key-vault-a-req"),
        .operation = "PutObject",
        .object_key = "first.bin"
    });
    ASSERT_TRUE(first.allowed);
    ASSERT_FALSE(first.reservations.empty());

    const auto secondVault = service.preflight({
        .vault_id = secondVaultId,
        .run_uuid = uniqueSuffix("gateway-key-vault-b"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = uniqueSuffix("gateway-key-vault-b-req"),
        .operation = "PutObject",
        .object_key = "second.bin"
    });
    EXPECT_TRUE(secondVault.allowed);
    EXPECT_FALSE(secondVault.reservations.empty());

    const auto firstVaultAgain = service.preflight({
        .vault_id = firstVaultId,
        .run_uuid = uniqueSuffix("gateway-key-vault-a-again"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.60000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = uniqueSuffix("gateway-key-vault-a-again-req"),
        .operation = "PutObject",
        .object_key = "first-again.bin"
    });
    EXPECT_FALSE(firstVaultAgain.allowed);
    EXPECT_TRUE(firstVaultAgain.stalled);
}

TEST(S3CostSafetyTest, GatewayPriceBudgetDecisionRetainsAllBlockingChecksAndPrimary) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway budget blocking-chain test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_blocking_chain");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(vaultId), suffix);
    const auto keyPolicy = saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        credentialId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000",
        false);
    const auto vaultPolicy = saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credentialId,
        vaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000",
        false);

    const auto runUuid = uniqueSuffix("gateway-blocking-chain-run");
    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = runUuid + "-request",
        .operation = "PutObject",
        .object_key = "blocking-chain.bin"
    });

    EXPECT_FALSE(decision.allowed);
    EXPECT_TRUE(decision.stalled);
    ASSERT_EQ(2u, decision.blocking_checks.size());
    EXPECT_TRUE(std::ranges::find(decision.blocking_policy_ids, keyPolicy.id) != decision.blocking_policy_ids.end());
    EXPECT_TRUE(std::ranges::find(decision.blocking_policy_ids, vaultPolicy.id) != decision.blocking_policy_ids.end());
    ASSERT_TRUE(decision.primary_blocking_check);
    EXPECT_EQ("monthly", decision.primary_blocking_window);
    EXPECT_EQ(keyPolicy.id, decision.primary_blocking_check->policy_id);
    EXPECT_EQ("gateway_credential", decision.primary_blocking_scope);
}

TEST(S3CostSafetyTest, GatewayPriceBudgetVaultPolicyBlocksEvenWhenCredentialHasRoom) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway budget vault blocker test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_vault_blocks");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(vaultId), suffix);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        credentialId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "10.00000000",
        false);
    const auto vaultPolicy = saveGenericBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::Vault,
        std::nullopt,
        vaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000");

    const auto runUuid = uniqueSuffix("gateway-vault-blocks-run");
    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = runUuid + "-request",
        .operation = "GetObject",
        .object_key = "vault-blocks.bin"
    });

    EXPECT_FALSE(decision.allowed);
    ASSERT_TRUE(decision.primary_blocking_check);
    EXPECT_EQ(vaultPolicy.id, decision.primary_blocking_check->policy_id);
    EXPECT_EQ("vault", decision.primary_blocking_scope);
    EXPECT_TRUE(decision.reservations.empty());
}

TEST(S3CostSafetyTest, GatewayPriceBudgetProviderPolicyBlocksEvenWhenCredentialVaultHasRoom) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway budget provider blocker test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_provider_blocks");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(vaultId), suffix);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credentialId,
        vaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "10.00000000",
        false);
    const auto providerPolicy = saveGenericBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::Provider,
        "aws-s3",
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000");

    const auto runUuid = uniqueSuffix("gateway-provider-blocks-run");
    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = runUuid + "-request",
        .operation = "PutObject",
        .object_key = "provider-blocks.bin"
    });

    EXPECT_FALSE(decision.allowed);
    ASSERT_TRUE(decision.primary_blocking_check);
    EXPECT_EQ(providerPolicy.id, decision.primary_blocking_check->policy_id);
    EXPECT_EQ("provider", decision.primary_blocking_scope);
}

TEST(S3CostSafetyTest, GatewayCredentialWarnBudgetAllowsAndWarns) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed gateway credential warn budget test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_key_warn");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(vaultId), suffix);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        credentialId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Warn,
        "0.10000000");

    const auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = uniqueSuffix("gateway-key-warn"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("1.00000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = uniqueSuffix("gateway-key-warn-req"),
        .operation = "GetObject",
        .object_key = "warn.bin"
    });

    EXPECT_TRUE(decision.allowed);
    ASSERT_FALSE(decision.warnings.empty());
    EXPECT_NE(std::string::npos, decision.warnings.front().find("would exceed monthly limit"));
}

TEST(S3CostSafetyTest, S3GatewayWsBudgetPolicyListDisableAndStatusForCredentialScopes) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway WS budget policy test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_ws_budget_policy");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerForVaultDbTest(vaultId), suffix);
    const auto session = superAdminWsSession();

    const auto keyPolicyResponse = vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential"},
        {"gateway_credential_id", credentialId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "2.00000000"},
        {"require_verified_catalog", false}
    }, session);
    ASSERT_TRUE(keyPolicyResponse.contains("policy"));
    EXPECT_EQ("gateway_credential", keyPolicyResponse["policy"]["scope"].get<std::string>());
    EXPECT_EQ(credentialId, keyPolicyResponse["policy"]["gateway_credential_id"].get<std::uint32_t>());

    const auto keyVaultPolicyResponse = vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", credentialId},
        {"vault_id", vaultId},
        {"mode", "warn"},
        {"currency", "USD"},
        {"max_monthly_cost", "1.00000000"},
        {"require_verified_catalog", false}
    }, session);
    ASSERT_TRUE(keyVaultPolicyResponse.contains("policy"));
    EXPECT_EQ("gateway_credential_vault", keyVaultPolicyResponse["policy"]["scope"].get<std::string>());
    EXPECT_EQ(vaultId, keyVaultPolicyResponse["policy"]["vault_id"].get<std::uint32_t>());

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "vault"},
        {"vault_id", vaultId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "3.00000000"},
        {"require_verified_catalog", false}
    }, session), std::exception);
    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyDisable({
        {"scope", "vault"},
        {"vault_id", vaultId}
    }, session), std::exception);

    vh::storage::s3::pricing::PriceBudgetPolicy genericVaultPolicy;
    genericVaultPolicy.scope = vh::storage::s3::pricing::PriceBudgetScope::Vault;
    genericVaultPolicy.vault_id = vaultId;
    genericVaultPolicy.mode = vh::storage::s3::pricing::PriceBudgetMode::Report;
    genericVaultPolicy.currency = "USD";
    genericVaultPolicy.max_monthly_cost = "3.00000000";
    genericVaultPolicy.require_verified_catalog = false;
    genericVaultPolicy.allow_stale_catalog = false;
    const auto savedGenericVaultPolicy = vh::storage::s3::pricing::PriceBudgetService{}.upsertPolicy(std::move(genericVaultPolicy));
    ASSERT_EQ("vault", vh::storage::s3::pricing::toString(savedGenericVaultPolicy.scope));

    const auto listed = vh::protocols::ws::handler::S3Gateway::budgetPolicyList({
        {"gateway_credential_id", credentialId},
        {"include_inactive", false}
    }, session);
    ASSERT_TRUE(listed.contains("policies"));
    EXPECT_GE(listed["policies"].size(), 2u);
    EXPECT_TRUE(std::ranges::any_of(listed["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId;
    }));
    EXPECT_TRUE(std::ranges::any_of(listed["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential_vault" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            policy.at("vault_id").get<std::uint32_t>() == vaultId;
    }));
    EXPECT_TRUE(std::ranges::none_of(listed["policies"], [](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "vault";
    }));

    const auto runUuid = uniqueSuffix("gateway-ws-budget-ledger");
    auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.25000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = runUuid + "-request",
        .operation = "PutObject",
        .object_key = "ws-managed.bin"
    });
    ASSERT_TRUE(decision.allowed) << decision.reason;
    ASSERT_FALSE(decision.reservations.empty());
    vh::storage::s3::pricing::PriceBudgetService{}.commit(decision.reservations, "0.25000000");

    const auto genericRunUuid = uniqueSuffix("gateway-ws-generic-ledger");
    auto genericDecision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = genericRunUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.12500000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });
    ASSERT_TRUE(genericDecision.allowed) << genericDecision.reason;
    ASSERT_FALSE(genericDecision.reservations.empty());
    vh::storage::s3::pricing::PriceBudgetService{}.commit(genericDecision.reservations, "0.12500000");

    const auto status = vh::protocols::ws::handler::S3Gateway::budgetStatus({
        {"gateway_credential_id", credentialId},
        {"vault_id", vaultId},
        {"limit", 10}
    }, session);
    ASSERT_TRUE(status.contains("policies"));
    ASSERT_TRUE(status.contains("ledger"));
    ASSERT_TRUE(status.contains("trends"));
    EXPECT_TRUE(std::ranges::any_of(status["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId;
    }));
    EXPECT_TRUE(std::ranges::any_of(status["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential_vault" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            policy.at("vault_id").get<std::uint32_t>() == vaultId;
    }));
    EXPECT_TRUE(std::ranges::any_of(status["ledger"], [&](const nlohmann::json& row) {
        return row.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            row.at("vault_id").get<std::uint32_t>() == vaultId &&
            row.at("operation").get<std::string>() == "PutObject" &&
            row.at("status").get<std::string>() == "committed";
    }));
    EXPECT_TRUE(std::ranges::any_of(status["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential" &&
            trend.at("window_type").get<std::string>() == "monthly" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("total_cost").get<std::string>() == "0.25000000" &&
            trend.at("remaining").get<std::string>() == "1.75000000";
    }));
    EXPECT_TRUE(std::ranges::any_of(status["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        if (!trend.contains("vault_id") || trend.at("vault_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential_vault" &&
            trend.at("window_type").get<std::string>() == "monthly" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("vault_id").get<std::uint32_t>() == vaultId &&
            trend.at("total_cost").get<std::string>() == "0.25000000" &&
            trend.at("remaining").get<std::string>() == "0.75000000";
    }));

    const auto vaultOnlyStatus = vh::protocols::ws::handler::S3Gateway::budgetStatus({
        {"vault_id", vaultId},
        {"limit", 20}
    }, session);
    EXPECT_TRUE(std::ranges::none_of(vaultOnlyStatus["policies"], [](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "vault";
    }));
    EXPECT_TRUE(std::ranges::none_of(vaultOnlyStatus["ledger"], [&](const nlohmann::json& row) {
        if (!row.contains("run_uuid") || row.at("run_uuid").is_null()) return false;
        return row.at("run_uuid").get<std::string>() == genericRunUuid;
    }));
    EXPECT_TRUE(std::ranges::none_of(vaultOnlyStatus["trends"], [](const nlohmann::json& trend) {
        return trend.at("scope").get<std::string>() == "vault";
    }));
    EXPECT_TRUE(std::ranges::any_of(vaultOnlyStatus["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        if (!trend.contains("vault_id") || trend.at("vault_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential_vault" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("vault_id").get<std::uint32_t>() == vaultId &&
            trend.at("total_cost").get<std::string>() == "0.25000000";
    }));

    const auto disabled = vh::protocols::ws::handler::S3Gateway::budgetPolicyDisable({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", credentialId},
        {"vault_id", vaultId}
    }, session);
    EXPECT_TRUE(disabled.at("disabled").get<bool>());

    const auto listedActive = vh::protocols::ws::handler::S3Gateway::budgetPolicyList({
        {"gateway_credential_id", credentialId},
        {"vault_id", vaultId},
        {"include_inactive", false}
    }, session);
    EXPECT_TRUE(std::ranges::any_of(listedActive["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId;
    }));
    EXPECT_TRUE(std::ranges::none_of(listedActive["policies"], [](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential_vault";
    }));
}

// Stage 0 S5: pricing.budget.status with only an owned gateway_credential_id (no vault_id) used to return the
// system-wide notification and override lists (listNotifications/listOverrides with vault_id = nullopt) to any
// credential owner. Non-super-admins now only get rows for vaults they can view.
TEST(S3CostSafetyTest, GenericPricingWsCredentialStatusDoesNotLeakOtherVaultsNotificationsOrOverrides) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed pricing status leak test due to missing environment variables.";
    ensureDbReady();

    const auto ownVaultId = seedS3VaultForDbTest(uniqueSuffix("status_leak_own"));
    const auto foreignVaultId = seedS3VaultForDbTest(uniqueSuffix("status_leak_foreign"));
    const auto ownerId = ownerForVaultDbTest(ownVaultId);
    const auto foreignOwnerId = ownerForVaultDbTest(foreignVaultId);
    ASSERT_NE(ownerId, foreignOwnerId);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerId, uniqueSuffix("status_leak_cred"));

    vh::storage::s3::pricing::PriceBudgetService service;
    const auto makeNotification = [&](const std::uint32_t vaultId, const std::string& title) {
        vh::storage::s3::pricing::PriceBudgetNotification notification;
        notification.type = "budget.warn_threshold";
        notification.severity = "warning";
        notification.title = title;
        notification.message = title;
        notification.vault_id = vaultId;
        return service.createNotification(std::move(notification));
    };
    const auto ownNotification = makeNotification(ownVaultId, "status leak own vault");
    const auto foreignNotification = makeNotification(foreignVaultId, "status leak foreign vault");

    const auto foreignPolicy = saveVaultBudgetPolicyForDbTest(
        foreignVaultId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Enforce,
        "0.10000000");
    const auto foreignOverride = service.requestOverride({
        .run_uuid = uniqueSuffix("status-leak-run"),
        .vault_id = foreignVaultId,
        .requested_by = foreignOwnerId,
        .reason = "status leak regression",
        .policy_ids = {foreignPolicy.id},
        .estimated_cost = "1.00000000",
        .currency = "USD",
        .ttl_minutes = 30
    });

    const auto status = vh::protocols::ws::handler::Pricing::status({
        {"gateway_credential_id", credentialId}
    }, wsSessionForUser(ownerId));

    ASSERT_TRUE(status.contains("notifications"));
    ASSERT_TRUE(status.contains("overrides"));
    EXPECT_TRUE(std::ranges::none_of(status["notifications"], [&](const nlohmann::json& row) {
        return row.at("id").get<std::uint32_t>() == foreignNotification.id;
    })) << "credential owner saw another vault's budget notification";
    EXPECT_TRUE(std::ranges::none_of(status["overrides"], [&](const nlohmann::json& row) {
        return row.at("id").get<std::uint32_t>() == foreignOverride.id;
    })) << "credential owner saw another vault's budget override";
    EXPECT_TRUE(std::ranges::all_of(status["notifications"], [&](const nlohmann::json& row) {
        return row.contains("vault_id") && !row.at("vault_id").is_null() &&
            row.at("vault_id").get<std::uint32_t>() == ownVaultId;
    }));
    EXPECT_TRUE(std::ranges::any_of(status["notifications"], [&](const nlohmann::json& row) {
        return row.at("id").get<std::uint32_t>() == ownNotification.id;
    })) << "owner's own vault notification should stay visible";
}

TEST(S3CostSafetyTest, GenericPricingWsGatewayCredentialStatusFiltersPoliciesAndTrends) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed generic pricing WS gateway credential filter test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gw_ws_generic_filter");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto otherVaultId = seedS3VaultForDbTest(uniqueSuffix("gw_ws_generic_filter_other_vault"));
    const auto ownerId = ownerForVaultDbTest(vaultId);
    const auto credentialId = seedGatewayCredentialForDbTest(ownerId, uniqueSuffix("gw_gen_a"));
    const auto otherCredentialId = seedGatewayCredentialForDbTest(ownerId, uniqueSuffix("gw_gen_b"));
    const auto ownerSession = wsSessionForUser(ownerId);

    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        credentialId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "3.00000000",
        false);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credentialId,
        vaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "2.00000000",
        false);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        credentialId,
        otherVaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "2.00000000",
        false);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredential,
        otherCredentialId,
        std::nullopt,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "3.00000000",
        false);
    saveGatewayBudgetPolicyForDbTest(
        vh::storage::s3::pricing::PriceBudgetScope::GatewayCredentialVault,
        otherCredentialId,
        vaultId,
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "2.00000000",
        false);

    const auto runUuid = uniqueSuffix("gw-generic-filter-primary");
    auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = runUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.25000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = runUuid + "-request",
        .operation = "PutObject",
        .object_key = "generic-filter-primary.bin"
    });
    ASSERT_TRUE(decision.allowed) << decision.reason;
    ASSERT_FALSE(decision.reservations.empty());
    vh::storage::s3::pricing::PriceBudgetService{}.commit(decision.reservations, "0.25000000");

    const auto otherRunUuid = uniqueSuffix("gw-generic-filter-other");
    auto otherDecision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = otherRunUuid,
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.75000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = otherCredentialId,
        .request_uuid = otherRunUuid + "-request",
        .operation = "GetObject",
        .object_key = "generic-filter-other.bin"
    });
    ASSERT_TRUE(otherDecision.allowed) << otherDecision.reason;
    ASSERT_FALSE(otherDecision.reservations.empty());
    vh::storage::s3::pricing::PriceBudgetService{}.commit(otherDecision.reservations, "0.75000000");

    const auto listed = vh::protocols::ws::handler::Pricing::policyList({
        {"gateway_credential_id", credentialId},
        {"include_inactive", false}
    }, ownerSession);
    ASSERT_TRUE(listed.contains("policies"));
    EXPECT_TRUE(std::ranges::any_of(listed["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId;
    }));
    EXPECT_TRUE(std::ranges::any_of(listed["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential_vault" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            policy.at("vault_id").get<std::uint32_t>() == vaultId;
    }));
    EXPECT_TRUE(std::ranges::none_of(listed["policies"], [&](const nlohmann::json& policy) {
        return policy.contains("gateway_credential_id") &&
            !policy.at("gateway_credential_id").is_null() &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == otherCredentialId;
    }));
    const auto vaultScopedListed = vh::protocols::ws::handler::Pricing::policyList({
        {"gateway_credential_id", credentialId},
        {"vault_id", vaultId},
        {"include_inactive", false}
    }, ownerSession);
    EXPECT_TRUE(std::ranges::none_of(vaultScopedListed["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential_vault" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            policy.at("vault_id").get<std::uint32_t>() == otherVaultId;
    }));

    const auto status = vh::protocols::ws::handler::Pricing::status({
        {"gateway_credential_id", credentialId},
        {"vault_id", vaultId},
        {"limit", 20}
    }, ownerSession);
    ASSERT_TRUE(status.contains("policies"));
    ASSERT_TRUE(status.contains("ledger"));
    ASSERT_TRUE(status.contains("trends"));
    EXPECT_TRUE(std::ranges::any_of(status["ledger"], [&](const nlohmann::json& row) {
        return row.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            row.at("run_uuid").get<std::string>() == runUuid;
    }));
    EXPECT_TRUE(std::ranges::none_of(status["ledger"], [&](const nlohmann::json& row) {
        return row.contains("gateway_credential_id") &&
            !row.at("gateway_credential_id").is_null() &&
            row.at("gateway_credential_id").get<std::uint32_t>() == otherCredentialId;
    }));
    EXPECT_TRUE(std::ranges::any_of(status["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("total_cost").get<std::string>() == "0.25000000";
    }));
    EXPECT_TRUE(std::ranges::any_of(status["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        if (!trend.contains("vault_id") || trend.at("vault_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential_vault" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("vault_id").get<std::uint32_t>() == vaultId &&
            trend.at("total_cost").get<std::string>() == "0.25000000";
    }));
    EXPECT_TRUE(std::ranges::none_of(status["trends"], [&](const nlohmann::json& trend) {
        return trend.contains("gateway_credential_id") &&
            !trend.at("gateway_credential_id").is_null() &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == otherCredentialId;
    }));
}

TEST(S3CostSafetyTest, S3GatewayWsNonAdminCredentialManagementIsScopedToPrincipal) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway WS credential permission test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gw_ws_cred");
    const auto actorUserId = seedS3CostUserForDbTest(suffix, "actor");
    const auto otherUserId = seedS3CostUserForDbTest(suffix, "other");
    const auto actorSession = wsSessionForUser(actorUserId);

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::credentialsCreate({
        {"name", "cross-user-" + suffix},
        {"principal_user_id", otherUserId},
        {"scope_mode", "user_access"}
    }, actorSession), std::exception);

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::credentialsCreate({
        {"name", "global-" + suffix},
        {"scope_mode", "global"}
    }, actorSession), std::exception);

    const auto noAssignAdminUserId = seedS3CostVaultAdminUserForDbTest(suffix, "no_assign");
    const auto noAssignAdminSession = wsSessionForUser(
        noAssignAdminUserId,
        vh::rbac::role::Admin::VaultAdmin(noAssignAdminUserId));
    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::credentialsCreate({
        {"name", "cross-user-admin-no-assign-" + suffix},
        {"principal_user_id", otherUserId},
        {"scope_mode", "user_access"}
    }, noAssignAdminSession), std::exception);

    const auto otherCredentialId = seedGatewayCredentialForDbTest(otherUserId, "other_" + suffix);
    const auto otherCredentialSelector = std::to_string(otherCredentialId);
    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::credentialsScopeUpdate({
        {"access_key", otherCredentialSelector},
        {"scope_mode", "user_access"}
    }, actorSession), std::exception);
    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::credentialsRevoke({
        {"access_key", otherCredentialSelector}
    }, actorSession), std::exception);

    const auto otherCredential = gatewayCredentialByIdForDbTest(otherCredentialId);
    ASSERT_TRUE(otherCredential.has_value());
    EXPECT_TRUE(otherCredential->enabled);
    EXPECT_EQ(otherUserId, otherCredential->principal_user_id);

    const auto ownedCredentialId = seedGatewayCredentialForDbTest(actorUserId, suffix + "_owned");
    const auto adminUserId = seedS3CostSuperAdminUserForDbTest(suffix, "scope_admin");
    const auto adminSession = wsSessionForUser(adminUserId, vh::rbac::role::Admin::SuperAdmin(adminUserId));
    const auto globalUpdate = vh::protocols::ws::handler::S3Gateway::credentialsScopeUpdate({
        {"access_key", std::to_string(ownedCredentialId)},
        {"scope_mode", "global"},
        {"principal_user_id", adminSession->user->id},
        {"default_vault_role_id", s3CostVaultRoleIdByName("reader")}
    }, adminSession);
    ASSERT_TRUE(globalUpdate.contains("credential"));

    const auto globalCredential = gatewayCredentialByIdForDbTest(ownedCredentialId);
    ASSERT_TRUE(globalCredential.has_value());
    EXPECT_EQ("global", globalCredential->scope_mode);
    EXPECT_EQ(adminSession->user->id, globalCredential->principal_user_id);
    ASSERT_TRUE(globalCredential->created_by);
    EXPECT_EQ(adminSession->user->id, *globalCredential->created_by);
    EXPECT_FALSE(vh::protocols::s3::ObjectStore::credentialAllows(
        vh::protocols::s3::AuthContext{
            .user = adminSession->user,
            .credential = *globalCredential,
            .credential_id = globalCredential->id,
            .access_key = globalCredential->access_key,
            .scope_mode = globalCredential->scope_mode,
            .dev_context = false
        },
        0,
        vh::rbac::permission::vault::FilesystemAction::Write));
}

TEST(S3CostSafetyTest, S3GatewayWsNonAdminBudgetManagementRequiresVaultAuthority) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway WS budget permission test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gateway_ws_non_admin_budget");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    const auto ownerId = ownerForVaultDbTest(vaultId);
    const auto outsiderId = seedS3CostUserForDbTest(suffix, "outsider");
    const auto ownerCredentialId = seedGatewayCredentialForDbTest(ownerId, suffix + "_owner");
    const auto ownerSession = wsSessionForUser(ownerId);
    const auto budgetOwnerSession = wsSessionForUser(ownerId, s3GatewayBudgetManagerRole(ownerId));
    const auto outsiderSession = wsSessionForUser(outsiderId);
    const auto adminSession = superAdminWsSession();

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential"},
        {"gateway_credential_id", ownerCredentialId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "1.00000000"},
        {"require_verified_catalog", false}
    }, ownerSession), std::exception);
    EXPECT_THROW(vh::protocols::ws::handler::Pricing::policyUpsert({
        {"scope", "gateway_credential"},
        {"gateway_credential_id", ownerCredentialId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "1.00000000"},
        {"require_verified_catalog", false}
    }, ownerSession), std::exception);
    EXPECT_THROW(vh::protocols::ws::handler::Pricing::policyDisable({
        {"scope", "gateway_credential"},
        {"gateway_credential_id", ownerCredentialId}
    }, ownerSession), std::exception);

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", ownerCredentialId},
        {"vault_id", vaultId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "1.00000000"},
        {"require_verified_catalog", false}
    }, ownerSession), std::exception);

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyDisable({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", ownerCredentialId},
        {"vault_id", vaultId}
    }, ownerSession), std::exception);

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", ownerCredentialId},
        {"vault_id", vaultId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "1.00000000"},
        {"require_verified_catalog", false}
    }, outsiderSession), std::exception);

    const auto keyPolicyResponse = vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential"},
        {"gateway_credential_id", ownerCredentialId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "1.00000000"},
        {"require_verified_catalog", false}
    }, adminSession);
    ASSERT_TRUE(keyPolicyResponse.contains("policy"));

    EXPECT_THROW(vh::protocols::ws::handler::S3Gateway::budgetPolicyDisable({
        {"scope", "gateway_credential"},
        {"gateway_credential_id", ownerCredentialId}
    }, ownerSession), std::exception);

    const auto keyVaultPolicyResponse = vh::protocols::ws::handler::S3Gateway::budgetPolicyUpsert({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", ownerCredentialId},
        {"vault_id", vaultId},
        {"mode", "enforce"},
        {"currency", "USD"},
        {"max_monthly_cost", "0.50000000"},
        {"require_verified_catalog", false}
    }, budgetOwnerSession);
    ASSERT_TRUE(keyVaultPolicyResponse.contains("policy"));
    EXPECT_EQ("gateway_credential_vault", keyVaultPolicyResponse["policy"]["scope"].get<std::string>());
    EXPECT_EQ(vaultId, keyVaultPolicyResponse["policy"]["vault_id"].get<std::uint32_t>());

    const auto disabled = vh::protocols::ws::handler::S3Gateway::budgetPolicyDisable({
        {"scope", "gateway_credential_vault"},
        {"gateway_credential_id", ownerCredentialId},
        {"vault_id", vaultId}
    }, budgetOwnerSession);
    EXPECT_TRUE(disabled.at("disabled").get<bool>());
}

TEST(S3CostSafetyTest, S3GatewayCliBudgetStatusJsonAndKeyOnlyAuthorization) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway CLI budget test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gw_cli_bud");
    const auto vaultId = seedS3VaultForDbTest(suffix);
    attachS3ProviderForDbTest(vaultId, "AWS");
    const auto ownerId = ownerForVaultDbTest(vaultId);
    const auto otherUserId = seedS3CostUserForDbTest(suffix, "other_credential_owner");
    const auto credentialId = seedGatewayCredentialForDbTest(ownerId, suffix + "_owner");
    const auto otherCredentialId = seedGatewayCredentialForDbTest(otherUserId, suffix + "_other");
    const auto router = s3GatewayShellRouterForDbTest();
    const auto owner = dryRunActor(ownerId, vh::rbac::role::Admin::None(ownerId));
    const auto budgetOwner = dryRunActor(ownerId, s3GatewayBudgetManagerRole(ownerId));
    const auto admin = superAdminWsSession()->user;
    const auto credentialArg = std::to_string(credentialId);
    const auto vaultArg = std::to_string(vaultId);

    const auto deniedSetKey = router->executeLine(
        "s3-gateway budget set-key " + credentialArg + " --monthly 2.00000000 --mode enforce",
        owner);
    EXPECT_NE(0, deniedSetKey.exit_code);
    EXPECT_NE(std::string::npos, deniedSetKey.stderr_text.find("admin.s3_gateway.manage_budgets"));

    const auto adminSetKey = router->executeLine(
        "s3-gateway budget set-key " + credentialArg + " --monthly 2.00000000 --mode enforce --currency USD",
        admin);
    ASSERT_EQ(0, adminSetKey.exit_code) << adminSetKey.stderr_text;

    const auto ownerSetKeyVaultDenied = router->executeLine(
        "s3-gateway budget set-key-vault " + credentialArg + " --vault " + vaultArg +
            " --monthly 1.00000000 --mode warn --currency USD",
        owner);
    EXPECT_NE(0, ownerSetKeyVaultDenied.exit_code);
    EXPECT_NE(std::string::npos, ownerSetKeyVaultDenied.stderr_text.find("admin.s3_gateway.manage_budgets"));

    const auto budgetOwnerSetKeyVault = router->executeLine(
        "s3-gateway budget set-key-vault " + credentialArg + " --vault " + vaultArg +
            " --monthly 1.00000000 --mode warn --currency USD",
        budgetOwner);
    ASSERT_EQ(0, budgetOwnerSetKeyVault.exit_code) << budgetOwnerSetKeyVault.stderr_text;

    const auto budgetOwnerSetOtherKeyVault = router->executeLine(
        "s3-gateway budget set-key-vault " + std::to_string(otherCredentialId) + " --vault " + vaultArg +
            " --monthly 0.50000000 --mode enforce --currency USD",
        budgetOwner);
    ASSERT_EQ(0, budgetOwnerSetOtherKeyVault.exit_code) << budgetOwnerSetOtherKeyVault.stderr_text;

    const auto ownerOtherStatus = router->executeLine(
        "s3-gateway budget status --key " + std::to_string(otherCredentialId) + " --vault " + vaultArg + " --json",
        owner);
    ASSERT_EQ(0, ownerOtherStatus.exit_code) << ownerOtherStatus.stderr_text;
    const auto parsedOwnerOtherStatus = nlohmann::json::parse(ownerOtherStatus.stdout_text);
    EXPECT_TRUE(std::ranges::any_of(parsedOwnerOtherStatus["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential_vault" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == otherCredentialId &&
            policy.at("vault_id").get<std::uint32_t>() == vaultId;
    }));
    EXPECT_TRUE(std::ranges::none_of(parsedOwnerOtherStatus["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == otherCredentialId;
    }));

    const auto ownerDisableOtherKeyVault = router->executeLine(
        "s3-gateway budget disable-key-vault " + std::to_string(otherCredentialId) + " --vault " + vaultArg,
        budgetOwner);
    ASSERT_EQ(0, ownerDisableOtherKeyVault.exit_code) << ownerDisableOtherKeyVault.stderr_text;

    auto decision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = uniqueSuffix("gateway-cli-budget-ledger"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.25000000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = credentialId,
        .request_uuid = uniqueSuffix("gateway-cli-budget-request"),
        .operation = "PutObject",
        .object_key = "cli-managed.bin"
    });
    ASSERT_TRUE(decision.allowed) << decision.reason;
    ASSERT_FALSE(decision.reservations.empty());
    vh::storage::s3::pricing::PriceBudgetService{}.commit(decision.reservations, "0.25000000");

    const auto genericPolicy = saveVaultBudgetPolicyForDbTest(
        vaultId,
        "aws-s3",
        vh::storage::s3::pricing::PriceBudgetMode::Report,
        "3.00000000",
        false);
    ASSERT_EQ(vaultId, *genericPolicy.vault_id);
    auto genericDecision = vh::storage::s3::pricing::PriceBudgetService{}.preflight({
        .vault_id = vaultId,
        .run_uuid = uniqueSuffix("gateway-cli-generic-ledger"),
        .provider_key = "aws-s3",
        .provider_supported = true,
        .estimate = budgetEstimateForDbTest("0.12500000"),
        .dry_run = false,
        .override_policy_ids = {},
        .gateway_credential_id = {},
        .request_uuid = {},
        .operation = {},
        .object_key = {}
    });
    ASSERT_TRUE(genericDecision.allowed) << genericDecision.reason;
    ASSERT_FALSE(genericDecision.reservations.empty());
    vh::storage::s3::pricing::PriceBudgetService{}.commit(genericDecision.reservations, "0.12500000");

    const auto status = router->executeLine(
        "s3-gateway budget status --key " + credentialArg + " --vault " + vaultArg + " --limit 10 --json",
        admin);
    ASSERT_EQ(0, status.exit_code) << status.stderr_text;
    const auto parsed = nlohmann::json::parse(status.stdout_text);
    ASSERT_TRUE(parsed.contains("policies"));
    ASSERT_TRUE(parsed.contains("ledger"));
    ASSERT_TRUE(parsed.contains("trends"));
    EXPECT_TRUE(std::ranges::any_of(parsed["policies"], [&](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "gateway_credential" &&
            policy.at("gateway_credential_id").get<std::uint32_t>() == credentialId;
    }));
    EXPECT_TRUE(std::ranges::any_of(parsed["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential" &&
            trend.at("window_type").get<std::string>() == "monthly" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("total_cost").get<std::string>() == "0.25000000";
    }));
    EXPECT_TRUE(std::ranges::any_of(parsed["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        if (!trend.contains("vault_id") || trend.at("vault_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential_vault" &&
            trend.at("window_type").get<std::string>() == "monthly" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("vault_id").get<std::uint32_t>() == vaultId &&
            trend.at("remaining").get<std::string>() == "0.75000000";
    }));

    const auto vaultOnlyStatus = router->executeLine(
        "s3-gateway budget status --vault " + vaultArg + " --limit 20 --json",
        admin);
    ASSERT_EQ(0, vaultOnlyStatus.exit_code) << vaultOnlyStatus.stderr_text;
    const auto parsedVaultOnly = nlohmann::json::parse(vaultOnlyStatus.stdout_text);
    EXPECT_TRUE(std::ranges::none_of(parsedVaultOnly["policies"], [](const nlohmann::json& policy) {
        return policy.at("scope").get<std::string>() == "vault";
    }));
    EXPECT_TRUE(std::ranges::none_of(parsedVaultOnly["ledger"], [](const nlohmann::json& row) {
        return !row.contains("gateway_credential_id") || row.at("gateway_credential_id").is_null();
    }));
    EXPECT_TRUE(std::ranges::none_of(parsedVaultOnly["trends"], [](const nlohmann::json& trend) {
        return trend.at("scope").get<std::string>() == "vault";
    }));
    EXPECT_TRUE(std::ranges::any_of(parsedVaultOnly["trends"], [&](const nlohmann::json& trend) {
        if (!trend.contains("gateway_credential_id") || trend.at("gateway_credential_id").is_null()) return false;
        if (!trend.contains("vault_id") || trend.at("vault_id").is_null()) return false;
        return trend.at("scope").get<std::string>() == "gateway_credential_vault" &&
            trend.at("gateway_credential_id").get<std::uint32_t>() == credentialId &&
            trend.at("vault_id").get<std::uint32_t>() == vaultId &&
            trend.at("total_cost").get<std::string>() == "0.25000000";
    }));

    const auto vaultOnlyLedger = router->executeLine(
        "s3-gateway budget ledger --vault " + vaultArg + " --limit 20 --json",
        admin);
    ASSERT_EQ(0, vaultOnlyLedger.exit_code) << vaultOnlyLedger.stderr_text;
    const auto parsedVaultOnlyLedger = nlohmann::json::parse(vaultOnlyLedger.stdout_text);
    EXPECT_TRUE(std::ranges::none_of(parsedVaultOnlyLedger, [](const nlohmann::json& row) {
        return !row.contains("gateway_credential_id") || row.at("gateway_credential_id").is_null();
    }));

    const auto ownerDisableKey = router->executeLine(
        "s3-gateway budget disable-key " + credentialArg,
        owner);
    EXPECT_NE(0, ownerDisableKey.exit_code);
    EXPECT_NE(std::string::npos, ownerDisableKey.stderr_text.find("admin.s3_gateway.manage_budgets"));
}

TEST(S3CostSafetyTest, S3GatewayCliScopeSetRetargetsPrincipalAndAuditsGlobalConversion) {
    if (!hasDbEnv()) GTEST_SKIP() << "Skipping db-backed S3 gateway CLI scope test due to missing environment variables.";
    ensureDbReady();

    const auto suffix = uniqueSuffix("gw_cli_scope");
    const auto ownerId = seedS3CostUserForDbTest(suffix, "owner");
    const auto credentialId = seedGatewayCredentialForDbTest(ownerId, suffix + "_owned");
    const auto router = s3GatewayShellRouterForDbTest();
    const auto noAssignAdminUserId = seedS3CostVaultAdminUserForDbTest(suffix, "cli_no_assign");
    const auto noAssignAdmin = dryRunActor(noAssignAdminUserId, vh::rbac::role::Admin::VaultAdmin(noAssignAdminUserId));
    const auto denied = router->executeLine(
        "s3-gateway creds create cli-no-assign-" + suffix + " --user " + std::to_string(ownerId),
        noAssignAdmin);
    EXPECT_NE(0, denied.exit_code);
    EXPECT_NE(std::string::npos, denied.stderr_text.find("admin.s3_gateway.assign_principal"));

    const auto adminUserId = seedS3CostSuperAdminUserForDbTest(suffix, "scope_admin");
    const auto admin = dryRunActor(adminUserId, vh::rbac::role::Admin::SuperAdmin(adminUserId));
    const auto credentialArg = std::to_string(credentialId);

    (void)s3CostVaultRoleIdByName("reader");
    const auto result = router->executeLine(
        "s3-gateway creds scope " + credentialArg + " set --scope global --user " + std::to_string(admin->id) + " --role reader",
        admin);
    ASSERT_EQ(0, result.exit_code) << result.stderr_text;

    const auto updated = gatewayCredentialByIdForDbTest(credentialId);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ("global", updated->scope_mode);
    EXPECT_EQ(admin->id, updated->principal_user_id);
    ASSERT_TRUE(updated->created_by);
    EXPECT_EQ(admin->id, *updated->created_by);
    EXPECT_FALSE(vh::protocols::s3::ObjectStore::credentialAllows(
        vh::protocols::s3::AuthContext{
            .user = admin,
            .credential = *updated,
            .credential_id = updated->id,
            .access_key = updated->access_key,
            .scope_mode = updated->scope_mode,
            .dev_context = false
        },
        0,
        vh::rbac::permission::vault::FilesystemAction::Write));
}

} // namespace vh::test::s3_cost_safety
