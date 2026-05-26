# Testing + CI Map

## GitHub Workflows

- `.github/workflows/build_and_test.yml`
  - runs on push/PR to `main`
  - uses composite runner action with:
    - core build
    - core tests
    - web setup/build/test
- `.github/workflows/release.yml`
  - runs on:
    - `workflow_dispatch`
    - tag push (`v*`)
  - release job is explicitly bound to GitHub `Production` environment
  - flow:
    - release-state validation (`python3 -m tools.release check`)
    - core build/tests
    - release-tooling unit tests
    - web setup + build + test
    - deterministic changelog artifacts (`raw`, `payload`)
    - canonical packaging action
    - artifact upload
    - optional GitHub Release attachment on tag builds

## Composite Actions

- `.github/actions/build`: Meson configure/compile/install for `core/`
- `.github/actions/setup_web`: pnpm + Node setup using `web/.nvmrc`, install deps
- `.github/actions/build_web`: installs private icons from `~/vaulthalla-web-icons`, then runs `pnpm build`
- `.github/actions/test_web`: run `pnpm test`
- `.github/actions/package`: canonical CI packaging wrapper (`python3 -m tools.release build-deb --output-dir ...`)

## Local Verification Shortcuts

Toolkit scripts:

- `bash .codex/scripts/verify.sh web`
- `bash .codex/scripts/verify.sh core`
- `bash .codex/scripts/verify.sh integration`
- `bash .codex/scripts/verify.sh release`
- `bash .codex/scripts/verify.sh all`
- `bash .codex/scripts/changed.sh all`

Core-specific:

- `make build`
- `make test`
- `make run_test`
- `make uninstall && make clean-full && make run_test`
- `ninja -C build-ci`
- `meson test -C build-ci --print-errorlogs`

Web-specific:

- `(cd web && pnpm dev)`
- `(cd web && pnpm test)`

Release/version-specific:

- `python3 -m tools.release check`
- `python3 -m tools.release sync --dry-run`
- `python3 -m tools.release changelog draft --format raw`
- `python3 -m tools.release changelog payload`
- `python3 -m tools.release changelog ai-draft --ai-profile <profile>`
- `python3 -m tools.release changelog ai-release --ai-profile <profile>`
- `python3 -m tools.release build-deb --dry-run`

## Integration Harness Notes

`core/tests/integrations/main.cpp` enables test mode paths, resets/seeds DB, starts selected runtime services, and runs CLI/FUSE integration suites. This surface is useful for validating command parsing + runtime behaviors end-to-end.

Use the make target for integration validation. The clean known-good sequence is:

```bash
make uninstall
make clean-full
make run_test
```

`make run_test` prepares the integration test environment and runs the harness
against `/tmp/vh_mount`. Prefer this path for FUSE dogfooding because it is
repeatable and isolated from production/dev systemd state.

Do not treat `/mnt/vaulthalla` as the integration harness mount. That is the
production/dev mount and can retain stale systemd/FUSE state from a previous
install if the dev install was not torn down. Use `/mnt/vaulthalla` only when
specifically checking the production-style mount.

If `/mnt/vaulthalla` dogfooding is required, the working-tree install path is:

```bash
make dev
id -nG | grep -qw vaulthalla
vh setup assign-admin
```

Then use `vh` normally before making FUSE calls. The CLI call after `make dev`
associates the current operator UID with the admin user; on this workspace that
UID is normally `1000`.

This manual path has a real agent reliability caveat. If the `vaulthalla` group
did not exist when the current shell session started, the install script may add
the user to the group but the current shell will not observe that membership
until a relogin/new session. Most sessions already have the group and work fine,
but production-mount smoke failures can be environmental even when the FUSE
integration harness passes. Prefer `make run_test` and `/tmp/vh_mount` for
reproducible FUSE validation.

Production/dev R2 dogfooding writes to the real configured R2 test bucket. Use a
unique smoke-test prefix and clean it up afterward. If leaked payload objects are
removed, also check whether `.vaulthalla/index-v1.json` only indexed those smoke
objects; otherwise the dev remote index manifest can be left stale. Prefer the
integration harness when the behavior under test does not require real R2.

When `dev.enabled` or test mode is active and `dev.init_r2_test_vault` is true,
initdb clears the configured `VAULTHALLA_TEST_R2_*` bucket before seed data is
created. This prevents old smoke-test objects or stale `.vaulthalla/index-v1.json`
state from reappearing as phantom files when the dev R2 vault indexes remote
state.

Avoid `bin/vh/install.sh` for working-tree dogfooding; it builds from the latest
apt package rather than the current checkout.
