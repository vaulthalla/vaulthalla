---
title: S3 Gateway Implementation Notes
description: Development notes for scoped S3 gateway credentials and gateway price-budget controls.
order: 940
status: draft
tags:
  - development
  - s3-gateway
  - cost-control
---

# S3 Gateway Implementation Notes

These notes summarize the scoped credential and per-key budget work on the `s3-endpoint` branch.

:::toc[On this page]{depth="3" theme="compact"}
:::

## What Changed

- Added migration `096_s3_gateway_credential_scope_and_budget.sql`.
- Added migration `097_s3_gateway_actual_usage_accounting.sql`.
- Extended `s3_gateway_credentials` with creator, effective principal, scope mode, description, and expiry fields.
- Added `enforce_budget_for_local_requests` to gateway credentials.
- Added `s3_gateway_credential_vault_scope` for per-vault list/read/write/delete/admin flags.
- Added gateway credential and gateway credential/vault price-budget scopes.
- Added gateway budget ledger columns for credential id, request UUID, operation, object key, estimated cost, usage source, and synthetic/local marker.
- Added `s3_gateway_sync_origin` rows for future attribution of sync work back to the gateway request/credential that created local-first work.
- Extended C++ S3 gateway query models, credential management, SigV4 auth context, object-store permission checks, router request flow, CLI commands, WebSocket handlers, and web console models/page.
- Added Playwright configuration and a focused S3 Gateway browser suite that runs against an already-running dev stack.

## Implemented

- Credentials can be `user_access`, `vault_allowlist`, or `global`.
- `principal_user_id` is the effective user for gateway authentication; legacy `user_id` remains for compatibility.
- Disabled and expired credentials are rejected during authentication.
- `vault_allowlist` checks require both a matching scope row/action flag and normal Vaulthalla RBAC.
- `global` credentials require admin creation and an admin principal path.
- Bucket-admin gateway operations keep RBAC validation separate from credential scope, so `vault_allowlist` credentials use `can_admin` for bucket delete/admin decisions instead of also requiring object-level `can_list` or `can_delete`.
- Bucket binding mode validation rejects local vaults bound as `remote_cache`/`remote_proxy` and S3/R2 vaults bound as `local`, preventing management UI or CLI actions from creating bindings that route pricing/budget checks to the wrong engine type.
- Remote-backed gateway operations estimate provider cost and evaluate existing global/provider/vault policies plus new per-key and per-key/vault policies only for actual upstream work performed inside the gateway request.
- Remote-backed gateway operations check the vault remote policy's existing S3/R2 request budgets through request-scoped upstream usage capture, so request-budget failures are distinguishable and do not leave committed price ledger rows.
- Request-scoped upstream usage capture avoids using global cumulative S3 controller metrics as the authoritative per-request accounting source. The current gateway provider calls are synchronous on the session worker thread, so the capture is thread-local and snapshots usage behind a mutex.
- Gateway price estimates honor the request storage class when provided, normalizing aliases such as `STANDARD_IA` through the configured provider profile instead of always using the vault default tier.
- Budget denials return S3 XML `AccessDenied` with HTTP 403. The XML message includes the exact blocking policy id, scope, window, limit, used-before amount, remaining-before amount, requested cost, currency, provider key, vault id, gateway credential id, operation, and request UUID.
- `PriceBudgetDecision` now retains all exceeded enforce checks in `blocking_checks` / `blocking_policy_ids` and exposes a deterministic primary blocker through `primary_blocking_check`, `primary_blocking_scope`, and `primary_blocking_window`.
- Ledger reservations include gateway credential and request context.
- Local-only gateway operations are treated as zero/unavailable remote cost and do not consume remote provider budgets.
- Local/cache/metadata hits are treated as zero actual upstream usage by default and do not consume provider/vault/global upstream price budgets or upstream request budgets.
- Gateway credential/key-vault budgets default to the same actual upstream usage as the provider budgets. If `enforce_budget_for_local_requests=true`, local/cache/sync-deferred requests are recorded as synthetic gateway usage for gateway credential scopes only.
- Synthetic local gateway usage is marked in ledger/status output with `synthetic=true` and `usage_source` values such as `metadata`, `local_file`, `local_cache`, and `sync_deferred`.
- Gateway LIST is metadata-only. It uses `s3_gateway_object`, filesystem metadata, and `remote_object_index`; it does not decrypt object bodies, compute plaintext MD5 ETags, refresh manifests, or call upstream S3/R2 LIST/HEAD/GET APIs.
- `vh s3-gateway bucket backfill <bucket>` performs explicit metadata-only gateway object-state backfill. `--calculate-etags` is the intentional local body-reading/decrypting path for plaintext MD5 ETag import and is never invoked by LIST.
- Gateway PUT/COPY/multipart completion writes Vaulthalla local state and gateway metadata first for remote-backed buckets. Sync owns eventual upstream upload/index/manifest side effects and the provider/vault/global upstream price-budget ledger rows for that work.
- Gateway DELETE now removes or tombstones Vaulthalla local state and clears gateway metadata without direct upstream delete calls from the S3 Router/ObjectStore layer. Sync owns upstream purge.
- Bucket delete emptiness checks use Vaulthalla metadata, filesystem rows, gateway metadata, and remote index state instead of only `s3_gateway_object`.
- CLI management exists under `vh s3-gateway creds scope ...` and `vh s3-gateway budget ...`.
- WebSocket management endpoints exist for service status, credentials, scopes, buckets, budget policy upsert/list/disable, ledger, and status.
- WebSocket credential create/update normalizes `user-access`, `vault-allowlist`, and their underscore forms to the canonical DB scope values, matching CLI behavior.
- Web console route `/s3-gateway` exists with service, credential, scope, bucket, budget, ledger, and client setup sections. The page has been split into `web/src/components/s3-gateway/*` components so later browser tests can target stable sections.
- DB-backed coverage includes disabled/expired credential rejection, scope row replacement/lookup, scoped action denial, and a signed S3 route returning XML `AccessDenied` when credential scope denies access after RBAC passes.
- DB-backed route coverage now distinguishes actual upstream use from local-first gateway work: remote-only GET commits actual upstream gateway ledger rows and can be denied by upstream request budget, LIST/local GET/local-first PUT/DELETE/multipart do not commit gateway ledger rows by default, synthetic local accounting can be enabled per credential, and request-budget `SlowDown` remains distinct from price-budget `AccessDenied`.
- DB-backed object-store coverage includes metadata-only local/remote LIST behavior, local-first remote delete with tombstones, no direct upstream delete from gateway DELETE, bucket-delete rejection from filesystem rows without gateway rows, bucket-delete rejection from remote index rows, and truly empty API-exclusive bucket deletion.
- DB-backed WebSocket coverage includes gateway budget policy upsert/list/disable/status for `gateway_credential` and `gateway_credential_vault` scopes.
- DB-backed WebSocket permission coverage includes non-admin denial for cross-user credential create/update/revoke, non-admin denial for global credential creation, non-admin denial for key-only budget cap management, outsider denial for key/vault policy management, and owner-managed key/vault policy disable.
- Gateway budget status trends are credential-aware for `s3.gateway.budget.status`, including current monthly used, remaining, and percent-used data for per-key and per-key/vault policies.
- WebSocket budget policy/status filtering preserves the key-wide cap when a caller scopes a view to both one credential and one vault, so UI/status views show both applicable monthly limits.
- S3 gateway budget WebSocket endpoints only manage `gateway_credential` and `gateway_credential_vault` policies; generic global/provider/vault price-budget policy mutation remains on the generic pricing handler, and gateway ledger/status output filters out non-gateway rows.
- Generic pricing WebSocket status/list endpoints accept `gateway_credential_id` and now consistently filter gateway policies, ledger rows, and trends to that credential; when a vault is also supplied, gateway credential/vault rows must match both dimensions. Generic pricing vault budget checks also honor direct vault ownership before falling back to admin vault permissions.
- S3 gateway CLI budget ledger/status output also filters out non-gateway price-budget rows, and vault-only status can show monthly trend rows for every `gateway_credential_vault` cap on that vault.
- Same-vault remote `CopyObject` uses one conservative `CopyObject` budget reservation instead of also reserving a separate source `GetObject`; cross-vault copies still budget source read and destination write separately.
- The S3 gateway web store refreshes budget status after policy save/disable and the page-level refresh path includes budget status, keeping current-month usage widgets aligned with management actions.
- The smoke script can exercise AWS CLI and MinIO `mc` clients when those binaries are present; set `S3_GATEWAY_SMOKE_REQUIRE_AWS=1` or `S3_GATEWAY_SMOKE_REQUIRE_MC=1` to require either client in a CI/live-smoke environment. It can either use an existing `S3_GATEWAY_SMOKE_API_KEY` or create a temporary upstream API key from `S3_GATEWAY_SMOKE_UPSTREAM_*` / `VAULTHALLA_TEST_R2_*` environment variables before creating a remote-cache bucket.
- The smoke script now supports `--local-only`, `--require-remote`, `--keep-resources`, and `--prefix <prefix>`. It defaults to unique `s3-gateway-test/<timestamp>-<pid>` object prefixes and reports endpoint, bucket, credential, mode, prefix, and remote cleanup status in a PASS/FAIL summary.
- `tools/smoke/s3_gateway_e2e.sh` wraps the Playwright S3 Gateway UI suite and the existing scoped-budget smoke script. The wrapper runs local-only smoke by default and runs the R2/S3 remote smoke only when remote config is present or `--require-remote` is passed.
- Admin CLI and WebSocket scope updates can retarget the effective principal; converting a credential to `global` stamps `created_by` with the admin actor so audit metadata matches runtime global-scope validation.
- Non-admin `vault_allowlist` creation/update now requires the principal to have real access to every named vault even if a submitted scope row has all action flags disabled. Individual enabled action flags are still validated against the principal's matching RBAC action.
- CLI gateway budget status supports JSON output with `policies`, `ledger`, and credential-aware `trends`; combined `--key --vault` status includes both the key-wide cap and the key/vault cap.
- CLI key-only budget cap creation/disable now requires admin permission; non-admin budget management is limited to key/vault caps where the caller owns or can manage the vault. This matches the WebSocket path and lets vault managers cap another principal's gateway key on their vault without seeing that key's key-wide budget cap.
- The generic pricing WebSocket policy handler also rejects non-admin key-wide `gateway_credential` mutations so callers cannot bypass the S3 gateway management endpoint's admin-only key cap rule.
- CLI usage metadata matches the scoped credential command contract: `allow-vault` and `revoke-vault` take positional vault arguments, and JSON flags are exposed only for gateway budget commands that return JSON.
- `runtime::Deps::init()` fills missing dependency slots instead of returning early when any dependency already exists, so CLI-only test helpers that initialize `shellUsageManager` do not prevent later S3 gateway DB fixtures from initializing storage dependencies in the same test process.
- `PriceBudgetPreflightRequest` keeps the existing sync preflight initializer prefix stable and appends gateway request metadata fields afterward, avoiding feature-introduced missing-initializer warnings in non-gateway pricing paths.

## Not Implemented Or Still Shallow

- The gateway price estimate path reuses the existing provider pricing catalog and sync estimate machinery where possible; it does not reconcile against provider invoices or live billing APIs.
- Gateway preflight commits usually use the estimated cost. Actual provider billable cost can differ if the provider applies request batching, free tier rules, lifecycle behavior, or provider-side copy billing that is not represented in the catalog.
- Web UI coverage now includes a focused Playwright suite for login/auth, admin navigation, page load, credential creation, secret reveal/hide, scope edit visibility, local bucket creation, key budget save, key/vault budget save, and invalid budget feedback. It intentionally avoids full-browser coverage of every gateway control.
- Route-level gateway budget tests cover core actual-vs-synthetic accounting paths, but provider-specific copy pricing and more exhaustive concurrent request interleavings can still be expanded.
- The R2-backed path is available through the opt-in smoke script when seeded/dev `VAULTHALLA_TEST_R2_*` or `S3_GATEWAY_SMOKE_UPSTREAM_*` configuration is present. It is skipped by default unless remote configuration exists or `--require-remote` is supplied.
- WebSocket gateway budget management has DB-backed coverage for core policy/status flows and representative non-admin permission denials, but additional role-matrix coverage could still be expanded.
- The management page exposes the core workflows but does not yet include advanced filtering, pagination, or notification drill-down.

## Validation Run

Validation performed during this branch work:

```bash
make test
meson compile -C build
./build/core/vh_unit_tests --gtest_filter='S3CostSafetyTest.*GatewayRemote*:S3CostSafetyTest.*GatewayPriceBudget*:S3CostSafetyTest.*PriceBudget*:S3CostSafetyTest.*RequestBudget*'
./build/core/vh_unit_tests --gtest_filter='S3GatewayDbTest.ObjectStore*List*:S3GatewayDbTest.DeleteBucket*'
VH_PATH_TO_CONFIG=/srv/vaulthalla/deploy/config/config.yaml ./vh_unit_tests --gtest_filter='S3CostSafetyTest.GatewayCredential*'
VH_PATH_TO_CONFIG=/srv/vaulthalla/deploy/config/config.yaml ./vh_unit_tests --gtest_filter='S3CostSafetyTest.GatewayRemote*Route*:S3CostSafetyTest.GatewayRemoteBudgetDeniedReturnsXmlAccessDeniedBeforeUpstreamPut'
VH_PATH_TO_CONFIG=/srv/vaulthalla/deploy/config/config.yaml ./vh_unit_tests --gtest_filter='S3GatewayObjectStoreTest.*:S3GatewayPricingTest.*:S3GatewayRouterTest.*:S3GatewaySigV4Test.*:S3GatewayDbTest.*'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3CostSafetyTest.S3GatewayWsNonAdminCredentialManagementIsScopedToPrincipal:S3CostSafetyTest.S3GatewayWsNonAdminBudgetManagementRequiresVaultAuthority:S3CostSafetyTest.S3GatewayWsBudgetPolicyListDisableAndStatusForCredentialScopes'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3CostSafetyTest.S3GatewayWsNonAdminCredentialManagementIsScopedToPrincipal:S3CostSafetyTest.S3GatewayCliScopeSetRetargetsPrincipalAndAuditsGlobalConversion'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3CostSafetyTest.S3GatewayCliBudgetStatusJsonAndKeyOnlyAuthorization:S3CostSafetyTest.S3GatewayWsBudgetPolicyListDisableAndStatusForCredentialScopes:S3CostSafetyTest.S3GatewayWsNonAdminBudgetManagementRequiresVaultAuthority'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3GatewayDbTest.SignedDeleteBucketUsesCanAdminWithoutCanDeleteScope:S3GatewayDbTest.VaultAllowlistAdminOperationsRequireCanAdminEvenForAdminPrincipal'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3GatewayDbTest.S3GatewayWebSocketRejectsRemoteModeForLocalVaultBinding'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3CostSafetyTest.S3GatewayWsNonAdminBudgetManagementRequiresVaultAuthority'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3CostSafetyTest.GatewayRemoteCopyRoutePreflightsAndCommitsCopyBudget:S3CostSafetyTest.S3GatewayWsBudgetPolicyListDisableAndStatusForCredentialScopes'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3CostSafetyTest.S3GatewayWsBudgetPolicyListDisableAndStatusForCredentialScopes:S3CostSafetyTest.S3GatewayCliBudgetStatusJsonAndKeyOnlyAuthorization'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3GatewayDbTest.S3GatewayWebSocketNormalizesCredentialScopeNames'
meson test -C build vh_unit_tests --print-errorlogs --test-args='--gtest_filter=S3PricingTest.GatewayEstimateUsesRequestStorageClassOverride'
pnpm --dir web run test
pnpm --dir web run test:e2e:s3-gateway
pnpm --dir web run typecheck
pnpm --dir web run lint
meson test -C build vh_unit_tests --print-errorlogs
make test
git diff --check
bash -n tools/smoke/s3_gateway_scoped_budget_smoke.sh
bash -n tools/smoke/s3_gateway_e2e.sh
```

In this shell, DB-backed unit cases compile but skip at runtime when `VH_TEST_DB_*` variables are not present. Playwright requires `VAULTHALLA_E2E_BASE_URL`, `VAULTHALLA_E2E_USER`, and `VAULTHALLA_E2E_PASSWORD` unless explicitly skipped with `VAULTHALLA_E2E_SKIP=1`. Live AWS CLI or MinIO `mc` smoke testing requires a configured gateway service and S3 client binary; remote-cache steps additionally require upstream S3/R2 test environment variables. The smoke scripts are the opt-in R2-backed validation path and delete only the configured unique prefix unless `--keep-resources` is supplied.

## Next Hardening Steps

1. Run `tools/smoke/s3_gateway_e2e.sh --require-remote` against a real local gateway and the seeded/dev R2 test bucket.
2. Expand WebSocket role-matrix tests for delegated vault admin roles beyond owner/super-admin cases.
3. Add provider-specific copy-object pricing refinements if catalogs expose provider-side copy pricing separately from GET plus PUT estimates.
4. Add deeper browser coverage for ledger filtering, disable flows, and permission-denied role states if those workflows start changing frequently.
