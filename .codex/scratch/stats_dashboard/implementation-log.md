# Stats Dashboard Implementation Log

## Phase 5 - Vault Activity and Mutation Stats

### Backend Files Added

- `core/include/stats/model/VaultActivity.hpp`
- `core/src/stats/model/VaultActivity.cpp`
- `core/include/db/query/vault/Activity.hpp`
- `core/src/db/query/vault/Activity.cpp`
- `core/src/db/preparedStatements/vault/activity.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/src/db/preparedStatements/fs/files.cpp`
- `core/src/db/preparedStatements/sync/operations.cpp`
- `core/src/db/query/fs/File.cpp`
- `core/include/db/query/sync/Operation.hpp`
- `core/src/db/query/sync/Operation.cpp`
- `core/src/sync/model/Operation.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `core/src/protocols/ws/handler/fs/Storage.cpp`
- `core/src/protocols/ws/Router.cpp`

### Frontend Files Added

- `web/src/models/stats/vaultActivity.ts`
- `web/src/components/vault/VaultStatsDashboard/VaultActivity/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.vault.activity`

### Dashboard Integration

- Vault dashboard order now includes:
  - Capacity
  - Sync Health
  - Activity

### Architectural Decisions

- Delete metrics use `files_trashed`, not `file_activity`, because deleting file rows cascades `file_activity`.
- Move/rename/copy stats are recorded in `operations` around web filesystem mutation commands.
- Operation failures are marked `error`, matching the database schema.
- File create/modify activity is recorded inside `File::upsertFile` and `File::updateFile` so web, share, and FUSE write paths contribute where they use those seams.
- Websocket `Router.cpp` was made namespace-explicit to fix a unity-build ambiguity exposed by adding new source files.

### Deferred TODOs

- Avoid duplicate rows before adding FUSE rename/move operation records.
- Add focused unit/integration coverage for activity recording.
- Keep share observability as Phase 6 rather than overloading the activity card.

## Phase 6 - Share Observatory Lite

### Backend Files Added

- `core/include/stats/model/VaultShareStats.hpp`
- `core/src/stats/model/VaultShareStats.cpp`
- `core/include/db/query/share/Stats.hpp`
- `core/src/db/query/share/Stats.cpp`
- `core/src/db/preparedStatements/share/stats.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `core/src/protocols/shell/commands/vault/listinfo.cpp`
- `core/src/protocols/shell/commands/vault/role.cpp`

### Frontend Files Added

- `web/src/models/stats/vaultShareStats.ts`
- `web/src/components/vault/VaultStatsDashboard/ShareStats/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.vault.shares`

### Dashboard Integration

- Vault dashboard order now includes:
  - Capacity
  - Sync Health
  - Activity
  - Share Observatory

### Architectural Decisions

- Share stats are read-only and query existing `share_link`, `share_upload`, and `share_access_event` tables.
- The payload intentionally excludes full IP/user-agent fields; this card surfaces operator posture without leaking unnecessary sensitive detail.
- Download counts use successful `share_access_event` rows whose event type starts with `share.download`.
- Upload counts use completed `share_upload` rows joined through `share_link` to the vault.
- Reconfigured unity builds exposed existing unqualified `Vault` ambiguity in shell vault commands, so the affected shell references now use `::vh::vault::model::Vault`.

### Deferred TODOs

- Add seeded DB tests for share rollup queries.
- Add an optional global share activity card later if the admin dashboard needs cross-vault share posture.

## Phase 7 - DB Health

### Backend Files Added

- `core/include/stats/model/DbStats.hpp`
- `core/src/stats/model/DbStats.cpp`
- `core/include/db/query/stats/DbStats.hpp`
- `core/src/db/query/stats/DbStats.cpp`
- `core/src/db/preparedStatements/stats/dbStats.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `core/include/db/query/share/Stats.hpp`
- `core/src/db/query/share/Stats.cpp`
- `core/include/db/query/vault/Activity.hpp`
- `core/src/db/query/vault/Activity.cpp`

### Frontend Files Added

- `web/src/models/stats/dbStats.ts`
- `web/src/components/stats/DbHealth.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`

### Websocket Commands Added

- `stats.system.db`

### Dashboard Integration

- Admin dashboard order now includes:
  - System Health
  - Thread Pools
  - FUSE Operations
  - Database Health
  - FS Cache
  - HTTP Cache

### Architectural Decisions

- DB health uses stock PostgreSQL catalog/stat views and does not require extensions.
- `pg_stat_statements` is detected, not assumed; slow-query count stays unavailable when the extension is not enabled.
- Failed DB collection returns a critical disconnected payload with an error instead of throwing away the entire health surface.
- The new `vh::db::query::stats` namespace required fully qualified `::vh::stats::model` references in older DB query headers to avoid shadowing.

### Deferred TODOs

- Add focused DB stats query tests.
- Add safe index bloat estimates later if the formula is backed by PostgreSQL metadata available in stock installs.

## Phase 8 - Vault Security / Integrity

### Backend Files Added

- `core/include/stats/model/VaultSecurity.hpp`
- `core/src/stats/model/VaultSecurity.cpp`
- `core/include/db/query/vault/Security.hpp`
- `core/src/db/query/vault/Security.cpp`
- `core/src/db/preparedStatements/vault/security.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`

### Frontend Files Added

- `web/src/models/stats/vaultSecurity.ts`
- `web/src/components/vault/VaultStatsDashboard/VaultSecurity/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.vault.security`

### Dashboard Integration

- Vault dashboard order now includes:
  - Capacity
  - Sync Health
  - Activity
  - Share Observatory
  - Security / Integrity

### Architectural Decisions

- Security posture is read-only and derives from existing key, file, share audit, and role policy tables.
- `encryption_status` is `unknown` when no current vault key exists, `mixed` when legacy/unknown file key versions remain, and `encrypted` only when current key coverage is complete.
- Share access pressure uses denied/failed/rate-limited `share_access_event` rows and keeps sensitive IP/user-agent data out of this payload.
- Permission-change timestamps are rollups from vault role assignments/overrides and share policy metadata.
- Integrity verifier state is exposed as `not_available`; the dashboard does not manufacture a checksum health claim.

### Deferred TODOs

- Add seeded DB coverage for vault security rollup edge cases.
- Add real checksum/integrity verification before reporting pass/fail integrity health.

## Phase 8A - Recovery Readiness

### Backend Files Added

- `core/include/stats/model/VaultRecovery.hpp`
- `core/src/stats/model/VaultRecovery.cpp`
- `core/include/db/query/vault/Recovery.hpp`
- `core/src/db/query/vault/Recovery.cpp`
- `core/src/db/preparedStatements/vault/recovery.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `core/src/protocols/shell/commands/vault/create.cpp`

### Frontend Files Added

- `web/src/models/stats/vaultRecovery.ts`
- `web/src/components/vault/VaultStatsDashboard/RecoveryReadiness/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.vault.recovery`

### Dashboard Integration

- Vault dashboard order now includes:
  - Capacity
  - Sync Health
  - Recovery Readiness
  - Activity
  - Share Observatory
  - Security / Integrity

### Architectural Decisions

- Recovery readiness is read-only telemetry from `backup_policy`; no backup job is started by the stats command.
- `recovery_readiness` is `unknown` with no policy row, `disabled` when the policy is disabled, `failing` for policy error or unresolved latest error, `stale` for missing/old success, and `ready` only with recent `last_success_at` evidence.
- Nullable `backup_stale` and missed-backup estimates keep no-policy state honest rather than presenting a false recoverability badge.
- The schema does not enforce one `backup_policy` row per vault, so the query uses the latest row by id.
- Reconfigure exposed a shell unity-build namespace ambiguity in `vault/create.cpp`; RBAC references now use fully qualified `::vh::rbac`.

### Deferred TODOs

- Add seeded DB tests for recovery status edge cases.
- Add verified-good backup checks only after a real backup verification process exists.

## Phase 8B - Operation Queue Health

### Backend Files Added

- `core/include/stats/model/OperationStats.hpp`
- `core/src/stats/model/OperationStats.cpp`
- `core/include/db/query/stats/OperationStats.hpp`
- `core/src/db/query/stats/OperationStats.cpp`
- `core/src/db/preparedStatements/stats/operationStats.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`

### Frontend Files Added

- `web/src/models/stats/operationStats.ts`
- `web/src/components/stats/OperationQueueStats.tsx`
- `web/src/components/vault/VaultStatsDashboard/OperationQueue/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.system.operations`
- `stats.vault.operations`

### Dashboard Integration

- Admin dashboard order now includes Operation Queue after FUSE Operations and before Database Health.
- Vault dashboard order now includes Operation Queue after Activity and before Share Observatory.

### Architectural Decisions

- System and vault operation stats share one `OperationStats` payload shape; `vault_id` is null for system scope.
- Filesystem queue metrics come from the `operations` table.
- Share upload queue metrics come from `share_upload`; vault scope joins through `share_link`.
- Stalled heuristics use a 15-minute age-only threshold because there is no upload progress timestamp instrumentation yet.
- Recent errors combine failed/cancelled operations and failed/cancelled uploads, limited to the latest rows.

### Deferred TODOs

- Add seeded DB tests for system/vault operation queue rollups.
- Add progress-stalled upload detection if upload progress instrumentation is introduced later.

## Phase 8C - Connection Health

### Backend Files Added

- `core/include/stats/model/ConnectionStats.hpp`
- `core/src/stats/model/ConnectionStats.cpp`

### Backend Files Changed

- `core/include/protocols/ws/ConnectionLifecycleManager.hpp`
- `core/src/protocols/ws/ConnectionLifecycleManager.cpp`
- `core/include/runtime/Manager.hpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `core/tests/unit/test_share_queries.cpp`

### Frontend Files Added

- `web/src/models/stats/connectionStats.ts`
- `web/src/components/stats/ConnectionStats.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`

### Websocket Commands Added

- `stats.system.connections`

### Dashboard Integration

- Admin dashboard order now includes Connection Health after FUSE Operations and before Operation Queue.

### Architectural Decisions

- Connection stats are read-only and derive from `auth::session::Manager::getActive()`.
- The payload separates human, share pending, share, and unauthenticated sessions.
- Timeout settings are read from `ConnectionLifecycleManager` through small accessors.
- IP and user-agent top lists are explicitly unavailable/empty in this phase to avoid unnecessary sensitive detail.
- 24h opened/closed/swept/error counters remain null because no lifecycle counters exist yet.
- Reconfigure exposed an incomplete-type unit-test dependency; `test_share_queries.cpp` now includes `share/Principal.hpp` directly.

### Deferred TODOs

- Add lifecycle counters for opened/closed/swept/error windows if needed.
- Add redacted/top-limited IP/user-agent summaries only after an explicit privacy choice.

## Phase 8D - Storage Backend Health

### Backend Files Added

- `core/include/stats/model/StorageBackendStats.hpp`
- `core/src/stats/model/StorageBackendStats.cpp`

### Backend Files Changed

- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `core/include/vault/model/Vault.hpp`
- `core/src/vault/model/Vault.cpp`

### Frontend Files Added

- `web/src/models/stats/storageBackendStats.ts`
- `web/src/components/stats/StorageBackendStats.tsx`
- `web/src/components/vault/VaultStatsDashboard/StorageBackend/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.system.storage`
- `stats.vault.storage`

### Dashboard Integration

- Admin dashboard order now includes Storage Backend after Operation Queue and before Database Health.
- Vault dashboard order now includes Storage Backend after Capacity and before Sync Health.

### Architectural Decisions

- Storage backend stats derive from live `storage::Manager` engines rather than new database tables.
- Per-vault snapshots include vault type, active state, `allow_fs_write`, quota, vault size, cache size, computed free-space signal, backend status, and S3 bucket/encryption config.
- `allow_fs_write` already existed in the schema but was not present in the runtime `Vault` model; the model now carries and serializes it.
- Provider operation/error/latency fields are returned as null because local/S3 provider operations are not instrumented yet.
- Backend status is `error` for missing/exceptional engines, `degraded` for inactive vaults, low free space, or incomplete S3 config, and `healthy` otherwise.

### Deferred TODOs

- Add provider operation counters and latency/error instrumentation only when storage engine boundaries are instrumented.
- Add focused backend unit tests for storage backend status classification.

## Phase 8E - Retention and Cleanup Pressure

### Backend Files Added

- `core/include/stats/model/RetentionStats.hpp`
- `core/src/stats/model/RetentionStats.cpp`
- `core/include/db/query/stats/RetentionStats.hpp`
- `core/src/db/query/stats/RetentionStats.cpp`
- `core/src/db/preparedStatements/stats/retentionStats.cpp`

### Backend Files Changed

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`

### Frontend Files Added

- `web/src/models/stats/retentionStats.ts`
- `web/src/components/stats/RetentionPressure.tsx`
- `web/src/components/vault/VaultStatsDashboard/RetentionPressure/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.system.retention`
- `stats.vault.retention`

### Dashboard Integration

- Admin dashboard order now includes Retention / Cleanup after Database Health.
- Vault dashboard order now includes Retention / Cleanup after Security / Integrity.

### Architectural Decisions

- Retention stats are read-only and derive from existing metadata tables; no cleanup or mutation is performed.
- System rollups query `files_trashed`, `sync_event`, `audit_log`, `share_access_event`, and `cache_index` globally.
- Vault rollups scope trash, sync events, share events, and cache directly by `vault_id`; audit logs are scoped through `audit_log.target_file_id -> fs_entry.vault_id`.
- Retention windows come from config: trash retention, sync event audit retention/max entries, audit log retention, thumbnail/cache expiry, and cache max size.
- Cache eviction candidates are expired entries unless total indexed cache bytes exceed configured max, in which case all indexed entries are candidates.
- Status is `healthy`, `warning`, `overdue`, or `unknown` based on past-retention backlog size and very-old item ages.

### Deferred TODOs

- Add seeded DB tests for system/vault retention rollups.
- Add share-access-event retention configuration if share event cleanup becomes independently configurable.

## Phase 9 - Historical Snapshots and Trends

Summary:

- Added `stats_snapshot` JSONB table with system/vault scope checks and indexes for scoped trend reads.
- Added configurable `stats_snapshots` config section:
  - `enabled`
  - `runtime_interval_seconds`
  - `vault_interval_seconds`
  - `retention_days`
- Added `StatsSnapshotService` as a runtime background `AsyncService`.
- The service collects runtime snapshots for:
  - `system.threadpools`
  - `system.fuse`
  - `system.cache`
  - `system.db`
- The service collects vault snapshots for:
  - `vault.capacity`
  - `vault.sync`
  - `vault.activity`
- Added snapshot insert, purge, and trend query layer under `db/query/stats/Snapshot`.
- Added trend model serialization under `stats/model/StatsTrends`.
- Added websocket commands:
  - `stats.system.trends`
  - `stats.vault.trends`
- Added frontend defensive trend parser and reusable trend card body.
- Admin dashboard renders Trends after Retention / Cleanup.
- Vault dashboard renders Trends after Retention / Cleanup.

Architectural decisions:

- Snapshot writes are intentionally done by a background service and are not part of FUSE/request hot paths.
- Trend commands read only `stats_snapshot`, not raw operational tables.
- System trends extract DB size/cache hit ratio/connections, FUSE total/error rate, thread-pool queue/pressure, and cache hit rates.
- Vault trends extract capacity growth, file count, sync errors/failed ops/bytes, and activity mutation/bytes-added counts.
- Frontend cards show no-data states until the collector has written snapshots instead of manufacturing fake trends.
- Daily compaction is deferred; retention is configurable and currently purges raw snapshots older than the configured window.

Checkpoint:

- Commit SHA: `aa4cf329`.
- Push target: `origin/stats-dashboards`.

## Phase 10 - Dashboard Registry, Overview Command, and Drilldown Routes

Summary:

- Added `stats.dashboard.overview` as an admin-only compact dashboard overview command.
- Added `DashboardOverview` backend model with section/card summaries, primary metrics, severity, warnings, errors, hrefs, availability, and attention queue entries.
- Added backend summary builders for:
  - `system.health`
  - `system.threadpools`
  - `system.connections`
  - `system.fuse`
  - `system.fs_cache`
  - `system.http_cache`
  - `system.storage`
  - `system.db`
  - `system.retention`
  - `system.operations`
  - `system.trends`
- Added frontend defensive dashboard overview parser and stats store wrapper/polling helpers.
- Replaced the giant `/dashboard` full-card list with compact overview cards and an attention queue.
- Added fixed drilldown routes:
  - `/dashboard/runtime`
  - `/dashboard/filesystem`
  - `/dashboard/storage`
  - `/dashboard/operations`
  - `/dashboard/trends`
- Moved existing full admin stats cards onto the drilldown routes without shrinking the existing components.
- Added static Dashboard child routes to the admin nav and exact matching for the overview sub-route.

Backend files added:

- `core/include/stats/model/DashboardOverview.hpp`
- `core/src/stats/model/DashboardOverview.cpp`

Backend files changed:

- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`

Frontend files added:

- `web/src/models/stats/dashboardOverview.ts`
- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/components/dashboard/DashboardDetailPage.tsx`
- `web/src/app/(app)/(admin)/dashboard/runtime/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/filesystem/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/storage/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/operations/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/trends/page.tsx`

Frontend files changed:

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/components/nav/NavList.tsx`
- `web/src/components/nav/types.d.ts`
- `web/src/config/nav/admin.ts`

Websocket commands added:

- `stats.dashboard.overview`

Architectural decisions:

- Summary severity/warning/error rules live in the backend overview builders.
- Frontend overview cards render only backend-provided summaries, metrics, warnings, errors, and hrefs.
- Overview request shape allows card ID/variant/size only and intentionally avoids arbitrary field selection.
- Summary builders reuse existing live stats collectors for buildout speed while returning compact payloads.
- Unavailable cards are honest `available=false` summaries and do not count as operational warnings/errors unless the underlying card builder marks them so.
- Global share stats remain omitted from the system operations drilldown because the roadmap has only landed per-vault share observability.
- Sidebar live severity badges are deferred; static child routes are in place.

Checkpoint:

- Commit SHA: `1b78ce7b`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 11 - Live Dashboard Severity Badges and Overview Polish

Summary:

- Added reusable dashboard severity helpers and fa-duotone severity icon components.
- Added reusable dashboard issue list rows for backend-provided warnings/errors/attention items.
- Polished `/dashboard` overview cards with severity icons, warning/error counts, first issue messages, and clearer detail links.
- Polished the attention queue with severity icons and backend-provided issue ordering/rendering.
- Added live dashboard nav severity badges driven by `stats.dashboard.overview` state.
- Added nav item metadata for Dashboard overall and section-level severity sources.

Frontend files added:

- `web/src/components/dashboard/dashboardSeverity.ts`
- `web/src/components/dashboard/DashboardSeverityBadge.tsx`
- `web/src/components/dashboard/DashboardIssueList.tsx`
- `web/src/components/nav/DashboardNavSeverityBadge.tsx`

Frontend files changed:

- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/components/nav/NavList.tsx`
- `web/src/components/nav/types.d.ts`
- `web/src/config/nav/admin.ts`

Websocket commands added:

- None. Phase 11 reuses `stats.dashboard.overview`.

Architectural decisions:

- Dashboard severity badge rendering uses only overview-provided severity/count/issue fields.
- No frontend thresholds or raw-stat business rules were added.
- `DashboardNavSeverityBadge` starts lightweight overview polling at 15 seconds while nav is mounted and relies on store dogpile protection.
- Compact nav mode renders a small count/dot indicator for dashboard severity.
- Focused helper tests are deferred because the web package does not currently have a unit test runner beyond typecheck/lint.

Checkpoint:

- Commit SHA: `284729c8`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 12 - Customizable Dashboard Home Layout

Summary:

- Added a browser-local dashboard layout model with finite card sizes and variants.
- Added a frontend card catalog for Phase 12 home-board customization.
- Reworked `/dashboard` overview cards into a responsive 12-column grid driven by the selected layout.
- Added customization controls for add/remove, Up/Down reorder, size changes, variant changes, and default reset.
- Extended dashboard overview store refresh/polling to carry the selected visible card specs to `stats.dashboard.overview`.

Backend files changed:

- None.

Frontend files added:

- `web/src/models/dashboard/dashboardLayout.ts`
- `web/src/components/dashboard/dashboardCardCatalog.ts`

Frontend files changed:

- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/stores/statsStore.ts`

Websocket commands added:

- None. Reuses `stats.dashboard.overview`.

Dashboard integration:

- Only `/dashboard` is customizable.
- Fixed detail routes remain unchanged and continue to render full rich cards.
- Attention queue now reflects currently visible home cards.

Architectural decisions:

- Layout is stored in browser-local `localStorage` as `vaulthalla.dashboard.layout.v1`.
- Invalid or all-hidden local layouts fall back to the default visible home board.
- Frontend selects only card IDs, variants, and sizes; backend remains authoritative for card summaries, metrics, severity, warnings, and errors.
- Server-side preference persistence and drag/drop are deferred to Phase 13.

Checkpoint:

- Commit SHA: `9fd1fbbe`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 13 - Persisted Dashboard Preferences and Drag/Drop

Summary:

- Added server-backed per-user dashboard home layout preferences.
- Added websocket get/update/reset preference commands.
- Added server-side layout JSON shape validation and preference-key normalization.
- Updated `/dashboard` to load server preference first, fall back to existing localStorage/defaults, and save/reset through the new commands.
- Added native HTML5 drag/drop card sequence reordering in customization mode while preserving Up/Down controls.
- Added built-in presets: Default, Minimal, Runtime, Storage, Operations, and Cockpit.

Backend files added:

- `deploy/psql/081_dashboard_preferences.sql`
- `core/include/stats/model/DashboardPreferences.hpp`
- `core/src/stats/model/DashboardPreferences.cpp`
- `core/include/db/query/dashboard/Preferences.hpp`
- `core/src/db/query/dashboard/Preferences.cpp`
- `core/src/db/preparedStatements/dashboard/preferences.cpp`
- `core/include/protocols/ws/handler/dashboard/Preferences.hpp`
- `core/src/protocols/ws/handler/dashboard/Preferences.cpp`

Backend files changed:

- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/protocols/ws/Handler.hpp`
- `core/src/protocols/ws/Handler.cpp`

Frontend files added:

- `web/src/models/dashboard/dashboardPreferences.ts`
- `web/src/stores/dashboardPreferencesStore.ts`

Frontend files changed:

- `web/src/models/dashboard/dashboardLayout.ts`
- `web/src/components/dashboard/dashboardCardCatalog.ts`
- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/util/webSocketCommands.ts`

Websocket commands added:

- `dashboard.preferences.get`
- `dashboard.preferences.update`
- `dashboard.preferences.reset`

Architectural decisions:

- The new table stores one JSONB layout object per `(user_id, preference_key)`.
- Preference commands require a human-authenticated websocket session and operate only on `session->user->id`.
- Backend validates shape/size/card-count but does not become the authoritative card catalog in this phase.
- Existing `localStorage` layout is used as a migration fallback only when no server preference exists.
- Drag/drop reorders the card sequence, not absolute grid positions.

Checkpoint:

- Commit SHA: pending.
- Push target: `origin/stats-dashboards`.
- Push result: pending.
