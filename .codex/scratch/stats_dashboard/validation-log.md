# Stats Dashboard Validation Log

## Phase 5 - Vault Activity and Mutation Stats

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after fixing websocket Router unity namespace ambiguity
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2
- Extra environment validation: `make dev` passed after dropping/recreating the existing dev PostgreSQL role/database and reseeding `/run/vaulthalla/db_password`.

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `e236717c`
- Push target: `origin/stats-dashboards`
- Push result: succeeded

## Phase 6 - Share Observatory Lite

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

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `36f35f23`
- Push target: `origin/stats-dashboards`
- Push result: succeeded

## Phase 7 - DB Health

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after qualifying DB query stats model namespaces
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `b681356c`
- Push target: `origin/stats-dashboards`
- Push result: succeeded

## Phase 8 - Vault Security / Integrity

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after renaming security query helpers for unity builds
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2 after sequential rerun

Known failures:

- An earlier concurrent `meson test -C build` run raced the test DB setup from `make test` and failed with password authentication for `vaulthalla_test`; the sequential rerun passed.

Checkpoint:

- Commit SHA: `f5ff4219`
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 8A - Recovery Readiness

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after qualifying shell vault create RBAC references
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `e0d97240`
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 8B - Operation Queue Health

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

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `f6ea80df`
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 8C - Connection Health

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after adding explicit `share/Principal.hpp` include in `test_share_queries.cpp`
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `60b02079`
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 8D - Storage Backend Health

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after adding the schema-backed `allow_fs_write` field to `vault::model::Vault`
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `b2d1dcb0`
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 8E - Retention and Cleanup Pressure

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed after making retention query helpers unity-build safe and including the concrete retention model in the websocket handler
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `c4338666`
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 9 - Historical Snapshots and Trends

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

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `aa4cf329`.
- Push target: `origin/stats-dashboards`
- Push result: succeeded, with GitHub remote moved warning

## Phase 10 - Dashboard Registry, Overview Command, and Drilldown Routes

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

Notes:

- An initial `meson test -C build` was started in parallel with `make test` while the test DB install was still running; it failed because `vh_cli_test` did not exist yet. Re-running serially after `make test` passed 2/2. This was a validation-order artifact, not a feature regression.

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `1b78ce7b`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 11 - Live Dashboard Severity Badges and Overview Polish

Validation:

- `git diff --check`: passed
- `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
- `meson setup --reconfigure build`: passed
- `meson compile -C build`: passed
- `make test`: passed
- `pnpm --dir web typecheck`: passed
- `pnpm --dir web lint`: passed
- `pnpm --dir web test`: passed
- `meson test -C build`: passed, 2/2 after rerun

Notes:

- Initial `meson test -C build` reported a single `S3ProviderIntegrationTest.test_S3MultipartUploadRoundtrip` `NoSuchKey` failure. Immediate serial rerun passed 2/2; treated as transient S3 fixture/provider behavior unrelated to Phase 11 frontend changes.

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `284729c8`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 12 - Customizable Dashboard Home Layout

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

Known failures:

- None currently.

Notes:

- Phase 12 is frontend-only. Backend validation was still run to preserve the normal phase cadence.
- `make test` completed after the expected test environment teardown/reinstall path.

Checkpoint:

- Commit SHA: `9fd1fbbe`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 13 - Persisted Dashboard Preferences and Drag/Drop

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

Known failures:

- None currently.

Notes:

- `make test` completed the expected integration teardown/reinstall path and exercised the new SQL migration.
- The first compile attempt failed because the new websocket preference handler only had a forward declaration of `DashboardPreference`; including the concrete model fixed JSON serialization.

Checkpoint:

- Commit SHA: `a2f8f51f`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 14 - Auth / Middleware Hardening for GitHub Issue #50

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

Notes:

- `make test` completed the expected integration teardown/reinstall flow.
- No route/middleware unit tests were added because the web package does not currently configure a unit test runner; the `test` script is `typecheck && lint`.

Known failures:

- None currently.

Checkpoint:

- Commit SHA: `e0845d96`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.

## Phase 15 - Dashboard Home UX Cleanup and Premium Polish

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

Known failures:

- None currently.

Notes:

- `make test` completed the expected integration teardown/reinstall flow.
- Phase 15 is frontend UX polish; backend validation was still run to preserve normal phase cadence.

Checkpoint:

- Commit SHA: `95865618`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
## Phase 16 - Dashboard Insight Cards and Visual Variants

- Commit: `82795125`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `meson setup --reconfigure build`: passed
  - `meson compile -C build`: passed
  - `make test`: passed
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `meson test -C build`: passed, 2/2
- Known failures: none.

## Phase 17 - Promote System Health to Command Bar and Fix Setup Advisory Severity

- Commit: `7af8666b`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `meson setup --reconfigure build`: passed
  - `meson compile -C build`: passed
  - `make test`: passed
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `meson test -C build`: passed, 2/2
- Known failures: none.

## Dashboard UX Corrective Pass - Command Center Layout, Picker, Density, and Visual Insight

- Commit: `c6a61f88`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `meson setup --reconfigure build`: passed
  - `meson compile -C build`: passed
  - `make test`: passed
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `meson test -C build`: passed, 2/2
- Known failures: none.

## Dashboard Home Card System Corrective Pass

- Commit: `bdc4a50b`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `meson setup --reconfigure build`: passed
  - `meson compile -C build`: passed
  - `make test`: passed
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `meson test -C build`: passed, 2/2
- Known failures: none.

## Dashboard Sidebar Build Fix

- Commit: pending.
- Push target: `origin/stats-dashboards`.
- Validation:
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web build`: passed
  - `git diff --check`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
- Known failures: none.

## Dashboard UX Stabilization - Deliverable A

- Commit: `4becf794`.
- Push target: `origin/stats-dashboards`.
- Push result: succeeded, with GitHub remote moved warning.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `pnpm --dir web build`: passed
- Known failures: none.

## Dashboard UX Stabilization - Deliverable B

- Commit: `927254e5`.
- Push target: `origin/stats-dashboards`.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `meson setup --reconfigure build`: passed
  - `meson compile -C build`: passed
  - `make test`: passed
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `pnpm --dir web build`: passed
  - `meson test -C build`: passed, 2/2
- Known failures: none.

## Dashboard Home Card Density Follow-up

- Commit: pending.
- Validation:
  - `git diff --check`: passed
  - `git -c core.filemode=true diff --summary`: passed, no filemode-only noise
  - `meson setup --reconfigure build`: passed
  - `meson compile -C build`: passed
  - `make test`: passed
  - `pnpm --dir web typecheck`: passed
  - `pnpm --dir web lint`: passed
  - `pnpm --dir web test`: passed
  - `pnpm --dir web build`: passed
  - `meson test -C build`: passed, 2/2
- Known failures: none.
