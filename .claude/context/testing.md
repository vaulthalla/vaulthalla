# Testing and local verification

## Surfaces → proof

| Changed | Minimum proof | Stronger proof |
|---|---|---|
| `core/**` C++ | `meson compile -C build` (unity build) | `meson test -C build --print-errorlogs` (unit, DB-backed), `make run_test` (integration harness) |
| `web/**` | `pnpm --dir web typecheck && pnpm --dir web lint` | `pnpm --dir web build` (needs private icons), Playwright `pnpm --dir web test:e2e` |
| `tools/release/**`, `VERSION`, managed files | `python3 -m tools.release check` | `python3 -m unittest discover -s tools/release/tests -p 'test_*.py'` |
| `debian/**`, `deploy/systemd`, `deploy/psql` | `bash tools/dev/verify.sh packaging` (runs the 69 orphaned packaging tests by module) | build a `.deb` and install/upgrade it on the lab VM (`/lab` skill) |
| `deploy/lifecycle/**` | `python3 -m unittest deploy.lifecycle.tests.test_main` (not run in CI) | lab VM `vh setup …` run |
| `docs/**` | `python3 .claude/skills/payload-markdown/scripts/check_payload_markdown_doc.py <files>` | `pmdocs validate --source docs` |
| `bin/**` shell | `bash -n <script>` | a real `make test` / `make dev` cycle, after asking first |

`bash tools/dev/verify.sh <profile>` wraps the common profiles. See the `/verify` skill.

## Build dirs

- `build/`: the working dev build (`meson setup build -Dbuild_unit_tests=true`) that CI also uses. `build-ci/`
  and `build-ci-release/` are older local mirrors. `make clean-full` wipes the build dirs.
- `make build` is **broken**: it calls `conan install . -r vaulthalla` but there is no conanfile. Use meson directly.
  `run-test` is declared `.PHONY` in the Makefile but has no recipe. Use `run_test`.

## DB-backed unit tests

1. `make test`. It uninstalls the test env, creates the test DB role and DB, and rewrites the generated `VH_TEST_DB_*`
   credentials into the ignored `deploy/vaulthalla.env`.
2. `set -a; source ./deploy/vaulthalla.env; set +a`
3. `./build/core/vh_unit_tests --gtest_filter='<Filter>'`

CLI ↔ ws parity for families migrated to `core/ops/`: `--gtest_filter='GroupParityTest.*:OpsGroups*'`. A parity case
runs one logical operation through `shell::Router::executeLine` and through the ws handler, for every seeded admin
role, and compares allow/deny (against an oracle from the role's permission bits) and resulting DB state, never text.
New families add a `test_ops_parity_<family>.cpp` on the same pattern.

Don't assume the shell `.bashrc` has current credentials. **Never print or commit secret values.** If you hit DB auth,
stale secrets, or port conflicts: `make uninstall` → `make test` → re-source.

## Integration harness (preferred for FUSE / CLI end-to-end)

```bash
make uninstall && make clean-full && make run_test   # destructive to local dev/test state
```

This runs `core/tests/integrations/main.cpp` in test mode: it wipes, inits, and seeds the DB, starts FUSE + shell, and runs the CLI and FUSE suites
against **`/tmp/vh_mount`**. The last known result was 64/64. It's isolated from systemd/prod state.

## Production-style mount dogfooding (`/mnt/vaulthalla`)

Only use this when the prod mount is specifically what's under test. `make dev` (a working-tree install; `bin/vh/install.sh` installs
the *apt* package, not your checkout) → `id -nG | grep -qw vaulthalla` → `vh setup assign-admin`. If the
`vaulthalla` group was created after your shell started, CLI failures are environmental; start a new login.
Dev mode requires both `VH_BUILD_MODE=dev` and the gitignored `enable_dev_mode` sentinel (`bin/lib/dev_mode.sh`).

## Other harnesses

- `tools/smoke/s3_gateway_{e2e,merge_ready,scoped_budget_smoke}.sh`: S3 gateway smoke runs against a live install.
- `tools/e2e/{load_env.sh,provision_e2e_user.sh}` + `web/tests/e2e` (Playwright).
- `tools/dev/share_preview_smoke.mjs`: public share preview smoke.
- Real R2 dogfooding writes to the real `vaulthalla-test` bucket. Use a unique prefix, clean it up, and check that
  `.vaulthalla/index-v1.json` isn't left stale.

## Known test gaps

- Web has no unit runner (`pnpm test` is typecheck + lint only).
- No seeded DB tests for the stats rollups or share stats. No operator-email dedupe, digest scheduler, or security-enqueue tests.
- DB loss/reconnect and pool exhaustion: `DBPoolReconnectTest` (`test_db_pool_reconnect.cpp`) kills pool backends
  with `pg_terminate_backend` (never restart the shared system PostgreSQL). Its unreachable-DB case needs a
  non-superuser test role that owns its DB, and skips otherwise. A burner DB works:
  `sudo -u postgres createdb -O <role> claude_burner_*`, then drop both afterwards.
- The full `vh_unit_tests` binary against a real DB runs about 230s (S3CostSafety plus S3GatewayDb take about 185s),
  so `meson test` hits its 120s timeout. Run `./build/core/vh_unit_tests` directly for the DB-backed run.

## Provider test credentials (Phase 2 scaffold)

- Canonical file: `/etc/vaulthalla/testing/providers.env` (root:vaulthalla 0640, dir 0750). Operator-managed and TEST-ONLY:
  dedicated S3 + R2 buckets with bucket-scoped keys. The package never ships, reads, rewrites or deletes it (upgrade and
  purge leave it; contract-tested), and the daemon never loads it.
- Template + tooling: `tools/lab/providers.env.example` (placeholder `CHANGE_ME` values; variable names are the
  `VAULTHALLA_TEST_{S3,R2}_*` ones core tests and `tools/smoke` already read, plus `VAULTHALLA_TEST_PROVIDER_PREFIX` /
  `VAULTHALLA_TEST_PROVIDER_DESTRUCTIVE` safety rails). `bash tools/lab/test_providers.sh install|check [--host vh-storage]`
  creates the scaffold without overwriting and reports set/placeholder/missing per variable, never values.
- Harness wiring: `tools/e2e/load_env.sh` sources it when readable (override path with `VH_TEST_PROVIDERS_ENV`). Run
  provider harnesses as a `vaulthalla`-group user or via sudo. Installed on vh-storage 2026-09-30 (unfilled).
- Guard: `python3 -m unittest tools.lab.tests.test_provider_scaffold_contract`.
