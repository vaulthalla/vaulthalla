# Architecture: core daemon, protocols, runtime

Verified against `main` @ 1.6.6 (2026-09-30). Merges the old `.codex` repo-overview,
core-backend, and protocol-map docs, with stale claims corrected.

## Build graph

- Root `meson.build` holds `project('vaulthalla', 'cpp', version: '1.6.6')`: `cpp_std=c++23`,
  `unity=on`, `unity_size=9`, `warning_level=3`, `werror=false`, `prefix=/usr`, meson >= 1.3.0.
  It enters C++ via `subdir('core')` and also installs `deploy/lifecycle` → `/usr/lib/vaulthalla/lifecycle`.
- `core/meson.build`: deps `fuse3 libsodium libcurl boost libpqxx pdfium yaml-cpp spdlog fmt
  openssl>=3 tss2-esys tss2-tctildr tss2-rc libmagic libjpeg libturbojpeg pugixml uuid zlib threads`.
  It generates `paths.h` (config/runtime/mount/state/log/psql paths, lines ~35-44).
- Artifacts: `vaulthalla-server`, `vaulthalla-cli`, `vh_usage` (manpage markdown generator),
  static libs `vaulthalla`, `vhseed`, `vhusage`, `vhusage-native`; tests `vh_unit_tests`, `vh_integration_tests`.
- `meson.options`: `manpage` (true), `install_data` (true), `build_unit_tests` (false),
  `integration_tests` (false), `systemd_system_unit_dir` (`/lib/systemd/system`), `udev_rules_dir`, `tmpfiles_dir`.
- Vendored code lives in `core/vendor/`. Seed data is in `core/seed/`, and test fixtures are in `core/test-assets/`.

### Unity-build hygiene (invariant)

Unity builds concatenate `.cpp` files, so file-scope `using namespace` leaks into neighbours. The
CI-only `reference to 'Vault' is ambiguous` failures came from filesystem-order-dependent grouping.
- Source discovery in `core/meson.build` is piped through `LC_ALL=C sort`. Keep it that way.
- **Don't add file-scope `using namespace` in `core/src/**/*.cpp`.** Wrap definitions in their
  namespace, or use narrow aliases. About 340 file-scope directives remain, so don't make it worse.
  A newer one is at `db/query/sync/RemoteObjectIndex.cpp:13`.
- Never weaken the unity, warning, or werror settings to get a build green.

## Process model

`core/main/main.cpp` boot sequence: config + log registries → DB init + prepared statements +
optional seed → runtime deps → storage wiring → `runtime::Manager` start → wait for SIGINT/SIGTERM.

`core/src/runtime/Manager.cpp` owns the service lifecycle. It runs a watchdog every 2s and restarts
a service after 500ms. Start order:

```
FUSE → SyncController → DBJanitor → LogRotationService → StatsSnapshotService →
OperatorEmailService → ConnectionLifecycleManager → ProtocolService → S3GatewayService (+ ShellServer)
```

Stop order is the reverse, with the ShellServer stopped right after ProtocolService.
ShellServer is not created in test mode (`paths::testMode`).

## Protocol surfaces (`core/src/protocols/`)

| Surface | Code | Default bind (shipped `deploy/config/config.yaml`) |
|---|---|---|
| WebSocket `/ws` | `ws/Server.cpp`, `ws/Router.cpp`, `ws/Handler.cpp`, `ws/handler/*`, `ws/Session.cpp` | `0.0.0.0:36969` |
| HTTP preview/download/upload/auth | `http/Server.cpp`, `http/Router.cpp`, `http/upload/Coordinator.cpp` | `0.0.0.0:36970` |
| S3 gateway (SigV4, multipart) | `s3/*` (`GatewayService`, `SigV4`, `ObjectStore`, `MultipartStore`, `CredentialManager`) | `0.0.0.0:39000`, disabled by default |
| Shell/CLI control | `shell/Server.cpp`, `shell/Router.cpp`, `shell/commands/*` | unix socket `/run/vaulthalla/cli.sock` |

> `core/include/config/Config.hpp` compiled-in defaults are **33369/33370**, but the shipped config,
> web fallbacks, and Caddyfile all use **36969/36970**. Trust the config file, and treat that
> header/config mismatch as a known wart.

### Shell CLI flow

`vh` / `vaulthalla` are symlinks to `vaulthalla-cli` (created by `bin/setup/install_dirs.sh` and the Debian payload).
1. `core/main/cli.cpp` normalizes args and connects to `/run/vaulthalla/cli.sock`.
2. It sends a JSON frame `{cmd, args, line, interactive}`.
3. `shell/Server.cpp` authenticates the peer by UID/group (the `vaulthalla` group plus the app-user UID mapping), answers
   with a `{"type":"hello"}` frame, then dispatches the frame. Each client runs on its own thread (cap 16, extra
   clients get exit 75 "busy"); request reads time out after 10s, prompt answers after 15 min, sends after 30s, and
   writes use `MSG_NOSIGNAL`. `status`/`version` skip the DB user lookup so they work while the DB is down.
4. `shell/Router.cpp` with `core/include/protocols/shell/Parser.hpp` tokenizes the line and runs the handler; output frames stream back.
   Before dispatch the Router rejects options the usage definitions don't declare: each option must be declared by
   a node on the path the positionals select (argument words are skipped), or match a declared `option_prefixes`
   family (role commands' generated `--allow-*`/`--deny-*`). `--help`/`-h` are always accepted.
   `test_cli_options.cpp` keeps definitions honest: every documented example must pass this check, and every
   subcommand name a handler dispatches on must be defined.
5. Usage/help comes from `core/usage/*` (root alias hard-coded as `vh`). The same code drives the `vh_usage` manpage
   generator and the integration-test command models, so CLI UX changes ripple into man pages and tests.

Invariants: the **daemon owns `/run/vaulthalla/cli.sock`** (binds it itself; every ~1s it checks the path still points
at its listener and rebinds if something replaced it). `deploy/systemd/vaulthalla-cli.{socket,service}` are obsolete
(`vaulthalla-cli --systemd` never existed as a mode; the unit connected to its own socket and hung) and should not be
shipped. The client waits at most `VAULTHALLA_CLI_TIMEOUT` (default 10s) for the first frame and exits 69 (no daemon),
75 (not responding), 77 (not in the `vaulthalla` group), 76 (reply without exit status). `vh status` exits 0/1/2 for
healthy/degraded/critical and includes a live `SELECT 1` DB probe.

### WebSocket flow

The web client builds `ws(s)://<host>/ws` in `web/src/util/getUrl.ts` (overridable with `NEXT_PUBLIC_VAULTHALLA_WS_ORIGIN`).
`web/src/stores/useWebSocket.ts` handles reconnect, the pending-request map keyed by `requestId`, and token injection.
Router allowlists are **exact and per session mode**: unauthenticated, human, pending-share, and ready-share.
Only the session-lifecycle commands (`auth.login`, `auth.logout`, `auth.refresh`, `auth.isAuthenticated`,
`auth.admin.default_password`) skip access-token validation (`isSessionLifecycleCommand`, mirrored by the web's
`SESSION_LIFECYCLE_COMMANDS`). Every other `auth.*` command (register, user update/delete/get/list, password change)
is account management and goes through `RequireHumanAuth`. A `starts_with("auth")` rule used to let unauthenticated
sockets reach handlers that dereference `session->user` (a remote daemon segfault); `WsAuthRouting.*` guards it.
While a human session's password still equals the seeded default, the Router serves only the
`default_password::isAllowedWhileDefault` commands (`protocols/ws/DefaultPasswordGate.cpp`, issue #103) and answers
everything else with `data.code = "password_change_required"`. `auth.login` is rate-limited per IP + account
(`ShareRateLimit.cpp`). The Router's debug log redacts credentials (`LogRedaction.cpp`); never log a raw ws message.
See `link-sharing.md`.

### HTTP auth/session proxy

`web/middleware.ts` calls `/api/auth/session` on the internal web origin (`VAULTHALLA_WEB_INTERNAL_ORIGIN`,
prod fallback `127.0.0.1:36968`). `web/src/app/api/auth/session/route.ts` proxies to
`VAULTHALLA_AUTH_ORIGIN` → `VAULTHALLA_PREVIEW_ORIGIN` → fallback `http://127.0.0.1:36970`.
(`NEXT_PUBLIC_SERVER_ADDR` no longer exists.)

### FUSE

`core/src/fuse/Service.cpp` validates the mountpoint, mounts a low-level session with `allow_other,auto_unmount`
(subtype `vaulthalla-fuse`), and dispatches to a thread pool. Mount path: `/mnt/vaulthalla` (prod/dev),
`/tmp/vh_mount` (integration harness).
- **FUSE ops hit the DB.** If the DB pool wedges, every FUSE op on the mount hangs, including
  `stat`, `mountpoint`, and `df`. See P0 in `production-hardening.md`.
- HTTP uploads stage `.upload-http-<id>-<file>.part` next to the target *through FUSE*, then rename
  from `fuse_from` to `fuse_to` (`http/upload/Coordinator.cpp`). This is intentional. Don't move staging out of
  FUSE to reduce sync churn; fix duplicate sync triggers or backing-path resolution instead.

## Database

- PostgreSQL via libpqxx. The schema is `deploy/psql/000…097_*.sql`, applied in order (all in ONE transaction by `core/seed/include/SqlDeployer.hpp`) and installed to `/usr/share/vaulthalla/psql`.
  New migrations take the next number and must be idempotent against upgraded installs. SqlDeployer records sha256(raw bytes)
  per file and refuses to start on a mismatch, so **never edit a shipped migration**: 020/060/082 were edited in place and
  bricked upgrades (1.5.x→1.6.x crash loop on 060). Reviewed exceptions live in `kHistoricalMigrationChecksums` (accepted, recorded
  hash rewritten to current, not re-run; a forward migration owns the delta). `core/seed/shipped_migrations.lock` pins every hash;
  `tools/release/tests/packaging/test_migration_checksums_contract.py` enforces it (plus every local v* tag).
- `core/include/db/DBPool.hpp` is a fixed pool of 4 connections (config `database.pool_size` is **not** wired to it)
  handed out as RAII `DBPool::Lease`s (FIFO) that always return the slot. A dead connection is replaced on
  `acquire()` (reconnect + re-prepare, pool-wide backoff 250ms→5s, callers inside the window get
  `DatabaseUnavailable`). `acquire()` throws `PoolAcquireTimeout` after 30s. libpq `connect_timeout=10`.
  `core/include/db/Transactions.hpp` has `Transactions::exec(ctx, fn)`, the only path to a `pqxx::work`. It
  reconnects and retries once only when BEGIN fails on a dead connection (before `fn` runs); later failures
  surface. Pool state is in `SystemHealth.database` (`vh status`, stats ws, watchdog). Queries live in
  `core/src/db/query/<domain>/`, prepared statements in `core/src/db/preparedStatements/`.
- `db::Janitor` handles sweeps. Stats rollups read from `file_activity`, `files_trashed`, `operations`, `share_*`.

## Subsystem directory map (`core/src`, mirrored in `core/include`)

`auth` sessions/tokens · `concurrency` thread pools · `config` YAML registry · `crypto` AES-GCM, TPM2/swtpm
key provider, secrets · `db` · `email` providers (Resend, SES v2) · `fs` · `fuse` · `identities` users/groups ·
`log` spdlog registries + rotation · `notifications` operator emails · `preview` thumbnails (pdfium,
turbojpeg) · `protocols` · `rbac` roles/permissions/resolver/actor · `runtime` Manager · `share` link
sharing · `stats` dashboard telemetry + snapshots · `storage` local + S3 backends, remote index · `sync`
controller, strategies `cache|sync|mirror`, cost guardrails · `vault` vault model, slugs, FUSE names ·
`ops` actor-authorized operations shared by the CLI and ws handlers (below).

### `ops/`: shared command operations

`core/{include,src}/ops/` holds plain free functions with typed request structs, one file pair per family
(`ops::groups`, `ops::roles`, ...). Each op takes the acting `User` (`ops::Actor`, null → `ops::Denied`), authorizes, looks up,
validates, persists, and returns domain objects. Refusals are typed `ops::Error`s (`Denied`, `NotFound`, `Invalid`,
`Conflict`, and `NeedsConfirmation{code}` for "a person must accept this first", e.g. the encryption waiver). The CLI
handler parses with `CommandUsage` and calls the op through `shell::runOp`, which maps `ops::Error` to exit 2 (CLI
waiver prompts go through `shell::commands::vault::runWithWaiver`). The ws handler maps its payload to the request,
and `makePayloadHandler` turns the exception into an `ERROR` response (`NeedsConfirmation` adds `data.code`; the web
asks and resends with `accept_encryption_waiver`). Rules: RBAC for an operation lives in the op, never in the frontend as well; code beneath
`ops::` (managers, `db::query`) never authorizes; internal callers use those primitives directly, not ops; no
registry, base class or transport abstraction. Parity is proven by `test_ops_parity_groups.cpp`, which runs each
group operation through both surfaces for every seeded admin role and compares verdicts and DB state.
Migrated families (each with `test_ops_parity_<family>.cpp`): `groups`, `roles`, `api_keys`, `vaults` (lifecycle +
sync policy), `users`, `s3_gateway` (credentials, grants, buckets, credential budgets), `pricing` (price budget
policies), `config` (every settings write: one validation, one apply step that restarts the S3 gateway when
`s3_gateway.enabled` changes). Still per-surface: the ws-only pricing preflight/override/notification endpoints,
email test-send/history, vault keys/sync diagnostics, and lifecycle commands (`setup`, `teardown`, `secrets`).

Rules the families hold (keep them in ops, never re-add them in a handler):
- **Users:** an account is an *admin identity* when its admin role grants anything outside the self scopes
  (`ops::users::isAdminIdentity`); that, not `User::isAdmin()` (a strict "full admin" gate used by S3 policy bypass and
  system stats), picks admins.* vs users.* identity permissions. The ceiling applies to assignment *and* to managing an
  account above you (edit, delete, reset password). Deletion, deactivation, role change and password reset call
  `auth::Manager::revokeSessions` (refresh tokens revoked, live sessions invalidated). `auth::Manager` has no user cache.
- **Vaults:** every change goes through `storage::Manager::updateVault` so the live engine (RBAC's source of the owner)
  follows; owner reassignment needs Create for the new owner; a key change needs Consume; sync settings need vault
  `sync.config.edit`.
- **S3 gateway:** the scope rule that was `CredentialManager::validateScopeMutation` is `requireScopeMutation` in ops;
  `CredentialManager` and `db::query::s3::Gateway` are trusted primitives. Vault names resolve owner-scoped or uniquely,
  never to the first match. Overrides need an explicit effect and pattern. Remote-cache buckets are created through
  `ops::vaults::create` (Consume, waiver) and rolled back if the bind fails.
- **Health:** `stats::model::SystemHealth::snapshot()` takes a bounded live DB probe; an unreachable database is
  critical there, so `vh status` and `stats.system.health` report the same severity.
- **Lists:** `ListQueryParams::sort` must be a column name (`isSortColumn`); it lands in ORDER BY.

**Role permissions (one mechanism).** Every permission change goes through
`PermissionResolver::applyChanges(role, exported, [(qualified, grant)], complete)`, which reports unknown names, missing
snapshot values and unapplicable permissions instead of skipping them. The CLI translates `--allow-*`/`--deny-*`
(the short flags `vh permission` prints, from each set's `flagPrefix()`) into that delta via
`shell/util/permissionFlags.hpp`; the web sends a complete `{qualified, value}` snapshot. The resolver only dispatches
a permission if its target trait (`TargetTraits.hpp`) or context policy (`policy/*.hpp`, included by `EnumPack.hpp`)
is visible; `test_role_permissions.cpp` round-trips every exported permission so a missing trait can't silently
no-op again. `ops::roles` enforces the escalation ceiling: nobody grants an admin permission they do not hold
(`permissionsBeyondActor`). Vault-role overrides persist through `db::query::rbac::permission::Override` on the
subject's assignment.

## Subsystem invariants (enforced in code, keep them)

**Operator email** (`email/`, `notifications/`, `085_operator_notifications.sql`, `vh email …`)
- Provider secrets are encrypted in `internal_secrets` and entered by hidden prompt. They never go in `.env` or files, and are never logged or rendered.
  Secret reveal was deliberately removed (`a7cc2f4b`).
- If email is disabled, the server must still report healthy. Security alerts are enqueued only after a successful DB write
  and never block the mutation. `Manager::startWatchdog()` stays restart-only.

**Stats / dashboards** (22 `stats.*` ws commands, `dashboard.preferences.*`)
- The backend owns severity, warning, and error truth. Never show fake integrity, recoverability, or latency badges; report
  unavailable values as `null` / `"not_available"`.
- Stats commands are read-only, and snapshots are background-only. Preferences are scoped to `session->user->id`.
- Rationale history: `history/stats-dashboard.md`.

**S3 pricing / cost estimates** (`core/{include,src}/storage/s3/pricing/`)
- Prices come from the **price bot**, a separate repo (`github.com/vaulthalla/vaulthalla-price-bot`, checked out at
  `/srv/vaulthalla-price-bot`, DB `vaulthalla_price_bot` on dev-db). It fetches official provider pricing, normalizes it,
  and publishes **Ed25519-signed** artifacts (`price-bot publish` / `mirror-artifacts`), served under `/v1/artifacts/`.
- Core `PriceBotClient` fetches `/v1/artifacts/manifest.json` plus the artifacts, verifies signatures with libsodium
  (`verifyEd25519Pem`), and caches under `<backing path>/price-cache`. `PriceCatalogStore` tries the primary source,
  then `fallback_artifact_base_urls`. `PriceEstimate` feeds price budgets (`091`–`094_s3_price_*.sql`).
- Config: `pricing.storage_rates_api` (`base_url: https://storage-rates-api.vaulthalla.cloud`,
  `remote_refresh_enabled: false` in the shipped config, `fail_open: true`, 12h cache/refresh). A local dev price bot
  runs on `127.0.0.1:36933`.
- Invariant: never trust unsigned or unverified price artifacts. Estimates are guidance and `fail_open`; enforcement modes act on
  them, so a pricing outage must not wedge sync.

**S3 cost safety** (sync + S3 gateway)
- Request budgets (LIST/HEAD/GET/PUT/COPY/DELETE/bytes) and price budgets (`off|report|warn|enforce`,
  global/provider/vault) are separate systems. Don't merge them.
- Dev R2 dogfooding hits a real bucket. With `dev.init_r2_test_vault`, initdb clears the `VAULTHALLA_TEST_R2_*` bucket.
