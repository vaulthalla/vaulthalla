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

- Commit SHA: `e0845d96`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 15 - Dashboard Home UX Cleanup and Premium Polish

### Summary

- Collapsed `/dashboard` top chrome into one compact Health Command Center strip.
- Removed the large Home Layout panel and the non-customizable section summary row.
- Made attention compact and conditional, showing only top visible issues and no large empty panel.
- Tightened summary card spacing, card heights, metric tiles, and issue rendering.
- Added frontend metric display preferences and hid low-value primary tiles such as Trends `Series` and `Points`.
- Added lightweight CSS meters for ratio-style metrics.
- Updated the dashboard grid to use multiple columns from `md`/`lg` instead of waiting for `xl`.
- Preserved server-backed preferences, presets, drag/drop, Up/Down controls, add/remove, size/variant controls, reset, save, and fixed drilldown routes.

### Files Changed

- `web/src/components/dashboard/DashboardOverview.tsx`
- `web/src/components/dashboard/DashboardIssueList.tsx`
- `web/src/components/dashboard/dashboardCardCatalog.ts`
- `web/src/app/(app)/(admin)/dashboard/page.tsx`
- `web/src/app/(app)/(admin)/dashboard/layout.tsx`
- `.codex/context/stats-dashboard.md`
- `.codex/scratch/stats_dashboard/status.md`
- `.codex/scratch/stats_dashboard/implementation-log.md`
- `.codex/scratch/stats_dashboard/validation-log.md`
- `.codex/scratch/stats_dashboard/open-questions.md`

### Dashboard Layout Decisions

- `/dashboard` is now one command strip plus the persisted configurable card grid.
- Attention is sourced from backend-provided overview issues and deduped by code/card/message for display.
- The admin sidebar was not converted to route-specific compact mode; the current sidebar belongs to the parent server-rendered admin layout, so this pass widened the dashboard content instead.

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

- Commit SHA: `95865618`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 16 - Dashboard Insight Cards and Visual Variants

- Status: committed and pushed.
- Commit SHA: `82795125`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Added the `visual` dashboard home card variant.
- Added dense metric tile rendering and lightweight CSS meters/stacked bars.
- Added frontend metric curation for home cards so low-value metrics such as Trends `series` and `points` are not primary dashboard tiles.
- Updated default layout and presets toward denser insight cards.
- Backend overview summaries now expose higher-signal keys for thread pool pressure, share sessions, local vaults, failed operations, and trend latest-sample/coverage.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `meson test -C build` 2/2

## Phase 17 - Promote System Health to Command Bar and Fix Setup Advisory Severity

- Status: committed and pushed.
- Commit SHA: `7af8666b`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- `system.health` removed from customizable dashboard home card catalog/defaults/presets.
- `/dashboard` still requests `system.health` for the pinned command bar health surface.
- Shell admin UID missing is now a setup/info advisory instead of runtime degradation.
- Existing saved layouts containing `system.health` normalize safely by filtering non-catalog cards and repairing to defaults if needed.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `meson test -C build` 2/2

## Dashboard UX Corrective Pass - Command Center Layout, Picker, Density, and Visual Insight

- Status: committed and pushed.
- Commit SHA: `c6a61f88`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Branch: `stats-dashboards`.
- Added dashboard-local top route toolbar with severity badges and removed dashboard child routes from the main admin sidebar.
- Replaced text-only Add Card dropdown flow with a visual card picker that supports size/variant selection and preview cards.
- Added normal-mode title-row drag handle that persists order through existing server-backed dashboard preferences.
- Tightened card and metric tile density while keeping warnings/errors visible.
- Added bounded grid row spans and finite sizes: `1x1`, `1x2`, `2x1`, `2x2`, `3x1`, `3x2`, `4x2`.
- Reworked visual variants so meter/stack visuals replace duplicated numeric metrics.
- Expanded meaningful visuals for operations, thread pools, connections, storage, retention, trends, FUSE, cache, and DB.
- Updated default layout and presets to prioritize Operation Queue, Storage Backend, Database Health, Retention, FUSE, Connection Health, Thread Pools, and FS Cache. Trends is not a default filler card.
- Expanded backend overview metric keys for higher-signal home-card curation.
- Preserved server-backed preferences, localStorage fallback/cache, presets, drag/drop, add/remove, size/variant controls, reset, save, and fixed drilldown routes.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `meson test -C build` 2/2

## Dashboard Home Card System Corrective Pass

- Status: committed and pushed.
- Commit SHA: `bdc4a50b`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Dashboard routes now use compact/icon-only admin sidebar mode; non-dashboard admin routes keep the full sidebar.
- Dashboard top toolbar spacing/styling was tightened into a uniform dashboard navigation strip.
- Home cards now use the dedicated `DashboardHomeCard` structure with bounded flex-column layout.
- Removed normal-card `View details >` footer; whole cards are clickable in normal mode.
- Card titles no longer have hidden drag-placeholder offset; the title row itself is the drag handle.
- Normal-mode drag reorder still persists through existing server-backed dashboard preferences.
- Metric capacity is size/variant-aware and visual cards cap metrics to avoid clipping.
- Visual variants omit metrics represented by the visual to avoid duplicate percentage/meter content.
- Default Operation Queue and Storage cards were reduced from `2x2` to `2x1`; Trends remains out of default.
- Real sparklines were deferred because overview payloads do not expose compact trend point arrays.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `meson test -C build` 2/2

## Dashboard Sidebar Build Fix

- Status: committed and pushed.
- Commit SHA: `9d1a1b58`.
- Push target: `origin/stats-dashboards`.
- Fixed `next build` failure caused by passing `adminNav` icon component functions into a Client Component.
- Restored `AdminSidebar` to server-rendered nav rendering.
- Added `AdminSidebarMode.client.tsx` as a tiny route-aware client shell that chooses between server-rendered compact and full sidebar variants.
- Validation passed:
  - `pnpm --dir web typecheck`
  - `pnpm --dir web build`
  - `git diff --check`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`

## Dashboard UX Stabilization - Deliverable A

- Status: committed and pushed.
- Commit SHA: `4becf794`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Dashboard/admin sidebar is now user-toggleable and dashboard routes auto-collapse by default unless the user explicitly selected full mode.
- Dashboard icon is corrected to a gauge icon; compact nav icons are centered.
- Dashboard top toolbar spacing/styling was tightened and uses the folder-tree icon for Filesystem.
- Home layout cards now use persisted `instanceId` identity so duplicate card families can coexist by variant.
- Exact same card/variant duplicates are prevented; older preferences without instance IDs normalize safely.
- Add Card picker now marks already-added card/variant combinations while allowing other variants of the same family.
- Metric capacity is normalized by configured card size with denser grid columns.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `pnpm --dir web build`

## Dashboard UX Stabilization - Deliverable B

- Status: committed and pushed.
- Commit SHA: `927254e5`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning. Context follow-up recorded in `5d049a65`.
- Dashboard overview card summaries now carry compact real graph series from stats snapshots.
- Added `graph` card variant.
- Added SVG sparkline rendering for graph cards.
- Thread Pools supports both numeric and graph variants, including aggregate pressure plus per-pool pressure lines from snapshots.
- FUSE, FS Cache, HTTP Cache, DB, and Trends can render graph variants from existing snapshot trend data.
- Operations remains a live stacked visual because operation queue snapshots do not exist yet.
- Default/presets updated so Thread Pools graph appears by default and Runtime/Cockpit demonstrate duplicate same-family numeric+graph variants.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `pnpm --dir web build`
  - `meson test -C build` 2/2

## Dashboard Home Card Density Follow-up

- Status: corrected after bad commit `78cd1d0a`; commit/push pending.
- Header/title/summary area is now separated from the metric/visual body area.
- Warnings/errors now render as compact inline title-row pills and no longer create body rows.
- Removed home-card body issue lists so warnings cannot push tiles off-card.
- Corrected the `78cd1d0a` regression where metric tiles stretched vertically to fill the card body.
- Metric tiles are compact fixed-height telemetry tiles (`h-11`/`h-12`), not height-aware blocks.
- Metric grids no longer use `auto-rows-fr`, metric links no longer use `h-full`, and hidden-count display no longer consumes a metric row.
- Large/sparse cards use existing visual abstractions to occupy body space instead of making one or two tiles huge.
- `2x2` metric capacity increased to 15 so larger cards do not hide a third row behind `+1 more`.
- Larger two-row card capacities were increased; `2x1` remains the compact one-row layout.
- Hidden metric counts now ignore low-value/demoted metrics and visual-represented metrics.
- Hidden metric counts render as an inline title-row chip rather than an awkward `+N more` grid tile.
- Fallback metric selection now skips low-value home metrics instead of using them as filler.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `pnpm --dir web build`
  - `meson test -C build` 2/2

## Dashboard Overview Component Refactor

- Status: implemented; commit/push pending.
- `DashboardOverview.tsx` reduced from 1498 lines to 441 lines.
- Added focused dashboard overview components under `web/src/components/dashboard/overview/`:
  - command bar
  - attention strip
  - grid
  - home card
  - metric tile
  - card visual
  - sparkline
  - card picker
  - customization controls
  - overview shell
- Added pure helper modules under `web/src/components/dashboard/overview/lib/`:
  - formatters
  - layout storage/defaults
  - layout capacity
  - overview payload construction
  - drag reorder
  - pending card fallback
- Preserved:
  - preference load/save/reset
  - localStorage fallback/cache
  - selected-layout overview polling
  - presets
  - drag/drop
  - add/remove
  - size/variant controls
  - command bar
  - attention strip
  - home cards
  - fixed drilldown pages
- No backend behavior, metrics, preference schema, graph features, auth/middleware, or drilldown page behavior changed.
- Validation passed:
  - `git diff --check`
  - `git -c core.filemode=true diff --summary`
  - `meson setup --reconfigure build`
  - `meson compile -C build`
  - `make test`
  - `pnpm --dir web typecheck`
  - `pnpm --dir web lint`
  - `pnpm --dir web test`
  - `meson test -C build` 2/2
  - extra `pnpm --dir web build`
