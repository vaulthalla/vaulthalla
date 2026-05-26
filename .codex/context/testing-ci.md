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
specifically checking the production-style mount, and run `make dev` first so
the local build is installed and the current operator UID has seeded access to
the default admin vaults. Avoid `bin/vh/install.sh` for this purpose; it builds
from the latest apt package rather than the current working tree.
