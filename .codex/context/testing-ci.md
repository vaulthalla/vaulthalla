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
- `bash .codex/scripts/verify.sh release`
- `bash .codex/scripts/verify.sh all`
- `bash .codex/scripts/changed.sh all`

Core-specific:

- `make build`
- `make test`
- `core/bin/tests/install.sh --run`

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
