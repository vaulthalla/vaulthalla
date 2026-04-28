# Release Tooling Contract

Persistent reference for `tools/release` + release CI behavior that future agents must preserve.

## Canonical Entrypoints

- CLI module: `python3 -m tools.release`
- CI packaging action: `.github/actions/package/action.yml`
- End-to-end release workflow: `.github/workflows/release.yml`

## Release Pipeline Shape

1. Build deterministic changelog evidence from git context.
2. Resolve changelog source by policy (`RELEASE_AI_MODE`).
3. Write selected changelog + selection metadata artifacts.
4. Refresh Debian changelog top entry when source is generated (not manual fallback).
5. Build Debian/web artifacts and validate release artifact contract.
6. Optionally publish Debian artifacts based on publication policy.

## Changelog Source Resolution (Stable)

`resolve_release_changelog` candidate order is mode-specific:

- `auto`: `openai` -> `local` -> `cached-draft` -> `manual`
- `openai-only`: `openai` -> `cached-draft` -> `manual`
- `local-only`: `local` -> `cached-draft` -> `manual`
- `disabled`: `cached-draft` -> `manual`

`manual` fallback is stale-checked against `VERSION` and must fail if stale.

## AI + Semantic/Triage Contracts

- Forensic payload schema version: `vaulthalla.release.ai_payload.v1`.
- Semantic payload schema version: `vaulthalla.release.semantic_payload.v1`.
- Triage schema version: `vaulthalla.release.ai_triage.v2`.
- `changelog ai-release` is a two-step path:
  - generate cached draft via `ai-draft`
  - finalize via `changelog release` with `RELEASE_AI_MODE=disabled` and cached draft source
- Triage path is semantic-first (`raw_semantic` by default) with emergency synthesis fallback before draft/polish stages.

## Artifact + Metadata Contract

Package action expects/produces:

- `changelog.release.md`
- `changelog.raw.md`
- `changelog.payload.json`
- `changelog.semantic_payload.json`
- `release_notes.md`
- `changelog.selection.json`

Selection metadata schema marker:

- `vaulthalla.release.changelog_selection.v1`

If selected path is `manual`, Debian top-entry refresh is intentionally skipped.

## Publication Policy

- `RELEASE_PUBLISH_REQUIRED=auto` resolves to:
  - `true` for tag refs (`refs/tags/*`)
  - `false` otherwise
- `publish-deb` with `--require-enabled` must fail when publication mode is disabled.
- Supported publication modes: `disabled`, `nexus`.

## Test Guardrails

High-signal tests future changes should keep green:

- `tools/release/tests/changelog/test_release_workflow.py`
- `tools/release/tests/changelog/test_ai_semantic_downstream_regression.py`
- `tools/release/tests/changelog/test_ai_provider_resolution.py`
- `tools/release/tests/changelog/test_ai_openai_client.py`
- `tools/release/tests/changelog/test_ai_openai_compatible_client.py`
- `tools/release/tests/packaging/test_debian_install_flow_contract.py`
- `tools/release/tests/packaging/test_release_artifact_validation.py`

## Safety/Determinism Expectations

- Unit tests must not require live OpenAI/local providers; provider calls are mocked/faked.
- Release tooling must keep explicit stale-check failures for manual/cached draft inputs.
- Changelog generation should remain deterministic-first: evidence artifacts are always written before source selection.
