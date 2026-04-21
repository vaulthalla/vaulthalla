# Release Tooling Map (`tools/release/`)

## Current Mode: Stable Release Maintenance

The release/changelog/package/install/publication spine is implemented and operational.  
Current work in this area is maintenance, diagnostics, and live-runtime validation follow-through (not foundational pipeline buildout).

## Canonical Entrypoints

- CLI: `python3 -m tools.release`
- Canonical packaging entrypoint: `.github/actions/package/action.yml`
- Canonical orchestration workflow: `.github/workflows/release.yml`

`package/action.yml` remains the single CI packaging path.  
`release.yml` wraps it with upload/publication/release-attachment policy.

## Current Release Spine

1. `tools.release check` validates release state.
2. Core + web build/test stages run.
3. Package action runs preflight checks and resolves changelog (`changelog.release.md`, `changelog.raw.md`, `changelog.payload.json`).
4. Package action builds and stages artifacts (`build-deb`) and enforces artifact contract checks (`validate-release-artifacts`).
5. Workflow uploads staged artifacts.
6. Publication policy is resolved (`RELEASE_PUBLISH_REQUIRED=auto|true|false`):
   - tag refs default to required publication under `auto`
   - non-tag runs default to optional publication under `auto`
7. Debian publication runs through `publish-deb` (Nexus or disabled mode).
8. Tag runs attach deduped staged assets to GitHub Release (`softprops/action-gh-release`, `overwrite_files: true`).

## Changelog Resolution Contract

Environment:

- `RELEASE_AI_MODE=auto|openai-only|local-only|disabled`
- `OPENAI_API_KEY`
- `RELEASE_AI_PROFILE_OPENAI`
- `RELEASE_LOCAL_LLM_ENABLED=true|false`
- `RELEASE_LOCAL_LLM_PROFILE`
- `RELEASE_LOCAL_LLM_BASE_URL`
- `RELEASE_LOCAL_LLM_API_KEY` (optional)

Deterministic provider order:

- hosted OpenAI
- local OpenAI-compatible endpoint (explicitly gated by `RELEASE_LOCAL_LLM_ENABLED=true`)
- cached local draft (`.changelog_scratch/changelog.draft.md`)
- manual/no-AI path with changelog stale check against `VERSION`

When set, `RELEASE_LOCAL_LLM_BASE_URL` explicitly overrides the local profile `base_url` and is logged.

Debian changelog generation contract:

- `python3 -m tools.release changelog release` writes release evidence artifacts and the selected release markdown.
- `python3 -m tools.release changelog ai-draft` writes a cached draft artifact under `.changelog_scratch` by default.
- `python3 -m tools.release changelog ai-release` runs AI draft generation and then finalizes Debian changelog from the freshly cached draft path (no manual copy/paste handoff).
- For generated paths (`openai` or `local`), it also refreshes the top `debian/changelog` entry as a full Debian record:
  - package name + full Debian version
  - distribution token
  - urgency token
  - summary bullet body
  - maintainer signature line
  - RFC-2822 timestamp
- Distribution/urgency resolution is explicit and validated:
  - CLI flags: `--debian-distribution`, `--debian-urgency`
  - env fallbacks: `RELEASE_DEBIAN_DISTRIBUTION`, `RELEASE_DEBIAN_URGENCY`
  - final fallback: existing top-entry values
- Manual/no-AI path (`debian/changelog` fallback) is stale-checked, explicitly logged as fallback, and does not blindly rewrite the changelog.

Scratch artifact policy:

- `.changelog_scratch/` is volatile local generation state, not canonical release truth.
- Scratch artifacts are cleared on version transitions driven by `set-version`, `bump`, and `sync` when upstream version alignment changes.

## Artifact/Packaging Contract

`build-deb` produces staged Debian and web deliverables.  
`validate-release-artifacts` enforces artifact classes and completeness checks, including Debian package payload and web archive runtime layout.

This contract validates more than "build completed": it checks shipped output completeness against current install/deploy expectations.

## Debian Install/Runtime/Lifecycle (High-Level)

- Web runtime is installed under `/usr/share/vaulthalla-web`.
- `vaulthalla-web.service` and runtime paths are aligned with packaged layout.
- Routine debconf prompt scaffolding is intentionally removed for default installs.
- Package install supports low-prompt default, lean `--no-install-recommends`, and explicit env-var opt-out overrides.
- Nginx integration is conservative and safe-skip oriented.
- Maintainer scripts now enforce explicit remove vs purge semantics and idempotent lifecycle handling.

## Publication and Release Attachment

Publication command:

- `python3 -m tools.release publish-deb --output-dir <artifact_dir> [--require-enabled]`

Config:

- `RELEASE_PUBLISH_MODE=disabled|nexus`
- `NEXUS_REPO_URL`, `NEXUS_USER`, `NEXUS_PASS`

Behavior:

- Optional runs may skip when disabled.
- Required runs (default on tags via policy resolution) fail if publication is disabled/misconfigured or upload fails.
- Nexus upload uses the configured base repository URL with explicit upload diagnostics.

GitHub release attachment:

- Uses a deduped staged asset list prepared in-workflow.
- Tag reruns are handled with overwrite mode to reduce attachment fragility.

## Intentionally Deferred

1. Phase 12b live APT/runtime validation on clean-host install/upgrade paths.
2. Broader repository promotion/orchestration beyond current Nexus upload boundary.
