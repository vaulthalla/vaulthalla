# Stats Dashboard Status

## Current Phase

- Phase 13 - Persisted Dashboard Preferences and Drag/Drop
- Status: committed and pushed.

## Completed Phases

- Phase 1: SystemHealth, `stats.system.health`, admin dashboard card.
- Phase 2: ThreadPool snapshots, `stats.system.threadpools`, admin dashboard card.
- Phase 3: FuseStats, `stats.system.fuse`, admin dashboard card.
- Phase 4: VaultSyncHealth, `stats.vault.sync`, vault dashboard card.
- Phase 5: VaultActivity, `stats.vault.activity`, vault dashboard card.
- Phase 6: VaultShareStats, `stats.vault.shares`, vault dashboard card.
- Phase 7: DbStats, `stats.system.db`, admin dashboard card.
- Phase 8: VaultSecurity, `stats.vault.security`, vault dashboard card.
- Phase 8A: VaultRecovery, `stats.vault.recovery`, vault dashboard card.
- Phase 8B: OperationStats, `stats.system.operations` and `stats.vault.operations`, admin and vault dashboard cards.
- Phase 8C: ConnectionStats, `stats.system.connections`, admin dashboard card.
- Phase 8D: StorageBackendStats, `stats.system.storage` and `stats.vault.storage`, admin and vault dashboard cards.
- Phase 8E: RetentionStats, `stats.system.retention` and `stats.vault.retention`, admin and vault dashboard cards.
- Phase 9: Historical snapshots, `stats.system.trends` and `stats.vault.trends`, admin and vault dashboard cards.
- Phase 10: Dashboard overview command, compact overview, drilldown routes, and dashboard nav child routes.
- Phase 11: Live dashboard severity badges and overview polish.
- Phase 12: Local customizable `/dashboard` home layout.

## Latest Phase Summary

Phase 13 persists the `/dashboard` home board server-side and adds drag/drop sequence reordering:

- Added `dashboard_preferences` with one `dashboard.home` row per user/key.
- Added authenticated current-user-only websocket commands for get/update/reset.
- `/dashboard` now loads server preferences, falls back to localStorage/defaults, and mirrors successful saves back to localStorage.
- Customize mode now supports built-in presets and drag/drop reordering while retaining Up/Down controls.
- Fixed drilldown pages remain unchanged and full-size.

No arbitrary metric field selection or drilldown customization was added.

## Checkpoint

- Commit SHA: `a2f8f51f`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 13 - Persisted Dashboard Preferences and Drag/Drop

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

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

## Phase 12 - Customizable Dashboard Home Layout

Backend files changed:

- None.

Frontend files added:

- `web/src/models/dashboard/dashboardLayout.ts`
- `web/src/components/dashboard/dashboardCardCatalog.ts`

Frontend files changed:

- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/stores/statsStore.ts`

Dashboard integration:

- `/dashboard` home uses a configurable browser-local summary-card grid.
- `/dashboard/runtime`, `/dashboard/filesystem`, `/dashboard/storage`, `/dashboard/operations`, and `/dashboard/trends` remain fixed detail pages.

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

## Phase 10 - Dashboard Registry, Overview Command, and Drilldown Routes

### Backend Files Added

- `core/include/stats/model/DashboardOverview.hpp`
- `core/src/stats/model/DashboardOverview.cpp`

### Backend Files Changed

- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`

### Frontend Files Added

- `web/src/models/stats/dashboardOverview.ts`
- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/components/dashboard/DashboardDetailPage.tsx`
- `web/src/app/(app)/(admin)/dashboard/runtime/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/filesystem/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/storage/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/operations/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/trends/page.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/components/nav/NavList.tsx`
- `web/src/components/nav/types.d.ts`
- `web/src/config/nav/admin.ts`

### Websocket Commands Added

- `stats.dashboard.overview`

### Dashboard Integration

- `/dashboard` renders `DashboardOverviewComponent` instead of the full scroll-heavy card list.
- `/dashboard/runtime` renders System Health, Thread Pools, and Connection Health.
- `/dashboard/filesystem` renders FUSE, FS Cache, and HTTP Preview Cache.
- `/dashboard/storage` renders Storage Backend, Database Health, and Retention / Cleanup.
- `/dashboard/operations` renders Operation Queue.
- `/dashboard/trends` renders Trends.
- Admin nav exposes Dashboard child routes for Overview, Runtime, Filesystem, Storage, Operations, and Trends.

### Architectural Decisions

- `stats.dashboard.overview` is admin-only.
- The backend owns severity, warning, and error rules for overview cards.
- The overview request accepts card IDs/variant/size only; it does not support arbitrary metric field selection.
- Summary builders reuse existing live stats collectors and return compact summaries.
- Unavailable cards are returned honestly with `available=false` and do not contribute to warning/error counts.
- Global share stats are omitted from Phase 10 operations because no global share card exists yet.
- Live severity badges in the server-rendered sidebar are deferred until the nav can consume live overview state without DOM scraping.

### Deferred TODOs

- Add live dashboard nav severity badges driven by `stats.dashboard.overview`.
- Add focused tests for dashboard overview summary severity mapping once a lightweight stats fixture seam exists.

## Phase 9 - Historical Snapshots and Trends

### Backend Files Added

- `deploy/psql/080_stats.sql`
- `core/include/stats/model/StatsTrends.hpp`
- `core/src/stats/model/StatsTrends.cpp`
- `core/include/db/query/stats/Snapshot.hpp`
- `core/src/db/query/stats/Snapshot.cpp`
- `core/src/db/preparedStatements/stats/snapshot.cpp`
- `core/include/stats/SnapshotService.hpp`
- `core/src/stats/SnapshotService.cpp`

### Backend Files Changed

- `core/include/config/Config.hpp`
- `core/src/config/Config.cpp`
- `core/include/config/config_yaml.hpp`
- `core/include/db/DBConnection.hpp`
- `core/src/db/Connection.cpp`
- `core/include/runtime/Manager.hpp`
- `core/src/runtime/Manager.cpp`
- `core/include/protocols/ws/handler/Stats.hpp`
- `core/src/protocols/ws/handler/Stats.cpp`
- `core/src/protocols/ws/Handler.cpp`
- `deploy/config/config.yaml`
- `deploy/config/config_template.yaml.in`

### Frontend Files Added

- `web/src/models/stats/statsTrends.ts`
- `web/src/components/stats/StatsTrends.tsx`
- `web/src/components/vault/VaultStatsDashboard/Trends/Component.tsx`

### Frontend Files Changed

- `web/src/util/webSocketCommands.ts`
- `web/src/stores/statsStore.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/components/vault/VaultStatsDashboard/Component.tsx`

### Websocket Commands Added

- `stats.system.trends`
- `stats.vault.trends`

### Dashboard Integration

- Admin dashboard order now includes Trends after Retention / Cleanup.
- Vault dashboard order now includes Trends after Retention / Cleanup.

### Architectural Decisions

- Snapshots are stored as JSONB in `stats_snapshot` with system/vault scope checks and created-at indexes.
- `StatsSnapshotService` is a runtime `AsyncService` and never touches FUSE or request hot paths.
- Runtime snapshots default to every 300 seconds and include `system.threadpools`, `system.fuse`, `system.cache`, and `system.db`.
- Vault snapshots default to every 3600 seconds and include `vault.capacity`, `vault.sync`, and `vault.activity`.
- Snapshot retention defaults to 30 days and is configurable under `stats_snapshots`.
- Trend websocket commands query only the snapshot table, not raw operational tables.
- Trend cards show 24h and 7d deltas and render a no-data state until snapshots exist.

### Deferred TODOs

- Add downsampled daily compaction if raw snapshots become too large for long retention windows.
- Add seeded DB tests for trend extraction once snapshot fixtures exist.

## Phase 11 - Live Dashboard Severity Badges and Overview Polish

### Backend Files Changed

- None.

### Frontend Files Added

- `web/src/components/dashboard/dashboardSeverity.ts`
- `web/src/components/dashboard/DashboardSeverityBadge.tsx`
- `web/src/components/dashboard/DashboardIssueList.tsx`
- `web/src/components/nav/DashboardNavSeverityBadge.tsx`

### Frontend Files Changed

- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/components/nav/NavList.tsx`
- `web/src/components/nav/types.d.ts`
- `web/src/config/nav/admin.ts`

### Websocket Commands Added

- None. Reuses `stats.dashboard.overview`.

### Dashboard Integration

- `/dashboard` summary cards and section cards now show fa-duotone severity icons and warning/error counts.
- Attention queue renders issue rows with severity icons and links.
- Dashboard nav parent and child routes render live severity badges from overview state.

### Architectural Decisions

- Frontend uses only backend-provided overview severity/count/issue fields.
- No raw metric thresholds or business rules were added to frontend code.
- Nav badge polling uses the shared stats store dogpile protection.
- Focused helper tests are deferred until a frontend unit test runner exists.

### Deferred TODOs

- Add focused helper tests once frontend unit testing is configured.
- Consider configurable nav severity polling cadence.

### Validation

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2 after rerun

## Phase 14 - Auth / Middleware Hardening for GitHub Issue #50

### Summary

- Hardened the Next `/api/auth/session` proxy route around private internal auth origins, bounded upstream fetches, clean unauthenticated response mapping, and safe failure logging.
- Hardened middleware route protection so it calls the private internal web origin, redirects quickly to `/login?next=...`, and bypasses login/share/API/static routes.
- Confirmed the production internal web fallback port remains `36968` from the packaged systemd service and nginx proxy config.

### Files Changed

- `web/src/app/api/auth/session/route.ts`
- `web/middleware.ts`
- `.codex/context/stats-dashboard.md`
- `.codex/scratch/stats_dashboard/status.md`
- `.codex/scratch/stats_dashboard/implementation-log.md`
- `.codex/scratch/stats_dashboard/validation-log.md`
- `.codex/scratch/stats_dashboard/open-questions.md`

### Issue #50 Requirements Covered

- Server-side auth proxy uses private `VAULTHALLA_AUTH_ORIGIN` / `VAULTHALLA_PREVIEW_ORIGIN`, not public `NEXT_PUBLIC_*` origins.
- Auth/session fetches and middleware checks are bounded at 2500 ms.
- Missing refresh cookie, upstream 401/403, and known unauthenticated upstream bodies return clean web 401 responses.
- Timeout/network upstream failures return clean 401 responses with `auth_upstream_unavailable`.
- Middleware redirects unauthenticated users to `/login` with the intended destination preserved in `next`.
- Failed upstream auth checks log status/reason/upstream path only, without cookies, auth headers, tokens, or raw bodies.

### Validation

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

### Checkpoint

- Commit SHA: pending.
- Push target: `origin/stats-dashboards`.
- Push result: pending.
