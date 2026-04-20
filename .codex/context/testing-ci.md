# Testing + CI Map

## GitHub Workflows

- `.github/workflows/build_and_test.yml`
  - runs on push/PR to `main`
  - uses composite runner action with:
    - core build
    - core tests
    - web setup/build/test
- `.github/workflows/release.yml`
  - runs on published release
  - runner action with `release: true` (includes package step)

## Composite Actions

- `.github/actions/build`: Meson configure/compile/install for `core/`
- `.github/actions/setup_web`: pnpm + Node setup using `web/.nvmrc`, install deps
- `.github/actions/build_web`: install private icons, run `pnpm build`
- `.github/actions/test_web`: run `pnpm test`
- `.github/actions/package`: release packaging flow

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

## Integration Harness Notes

`core/tests/integrations/main.cpp` enables test mode paths, resets/seeds DB, starts selected runtime services, and runs CLI/FUSE integration suites. This surface is useful for validating command parsing + runtime behaviors end-to-end.
