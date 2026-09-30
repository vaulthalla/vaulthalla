# Release pipeline: versioning, CI, packaging, publication

## Versioning

`VERSION` is canonical (currently `1.6.6`). Release-managed files: `meson.build`, `web/package.json`,
`debian/changelog` (top entry version `X.Y.Z-1`). A version bump commit touches exactly those 4 files.
Tags are `vX.Y.Z`, and pushing a tag triggers the release workflow.

## `python3 -m tools.release`

`check` · `sync [--dry-run]` · `set-version X.Y.Z` · `bump {major,minor,patch}` · `build-deb [--dry-run]
[--output-dir]` · `validate-release-artifacts` · `publish-deb [--require-enabled]` ·
`resolve-release-notes-base` · `record-release-success` · `changelog {draft, payload, release, ai-check,
ai-draft, ai-release, ai-compare}`.
Python deps: repo-root `requirements.txt`. AI model profiles: repo-root `ai.yml`.

## Changelog selection

The pipeline is deterministic first. Evidence artifacts are always written **before** source selection.
Candidate order by `RELEASE_AI_MODE`:
- `auto`: openai → local → cached-draft → manual
- `openai-only`: openai → cached-draft → manual
- `local-only`: local → cached-draft → manual
- `disabled`: cached-draft → manual

The `manual` source is stale-checked against `VERSION` and must fail when stale. When `manual` is selected, the Debian top-entry
refresh is skipped. Schemas: `vaulthalla.release.ai_payload.v1`, `semantic_payload.v1`,
`ai_triage.v2`, `changelog_selection.v2`. `changelog ai-release` = `ai-draft`, then `changelog release`
with `RELEASE_AI_MODE=disabled` using the cached draft.

Changelog relevance scoring (`tools/release/changelog/scoring.py` `is_semantic_noise_path`) treats all agent
tooling (`CLAUDE.md`, `.claude/`, and the legacy `.codex/` and `.agents/`) as derived noise. It's pinned by
`tests/changelog/test_scoring_noise_paths.py`. Keep both current if the agent layout moves again.

## GitHub workflows

- `build_and_test.yml`: push/PR to `main`. It runs the composite `runner` action (core build → core tests → web
  setup/build/test). CI builds in `build/` with `meson setup build -Dbuild_unit_tests=true -Dinstall_data=false`.
- `release.yml`: tag `v*` or `workflow_dispatch` (inputs `ref`, `publish`, `release_notes_base`). 10 jobs:
  `validate-release-state → core-verify, release-tooling-verify, web-verify, docs-validate →
  release-artifacts → publish-debian, github-release, docs-publish → record-release-success`.
  - The GitHub `Production` environment gates `publish-debian`, `docs-publish`, `record-release-success`.
  - `RELEASE_PUBLISH_REQUIRED=auto` resolves to true on tag refs or dispatch `publish=publish` (resolved in the workflow, ~L134).
  - Publication modes: `disabled` | `nexus`. The APT repo is `apt.vaulthalla.sh` (Nexus). `publish-debian` verifies APT metadata after upload.
  - Docs: `pmdocs validate --source docs` then `pmdocs push` to `DOCS_SYNC_ENDPOINT` (vaulthalla.io).
- Composite actions (`.github/actions/`): `runner` (flag-driven orchestrator), `build`, `test`, `setup_web`,
  `build_web` (calls `sync_web_icons`, then `pnpm install` + build), `test_web`, `package`.
  The runners are self-hosted, since they need the private icon dir.

## Package action artifact contract

`resolve-release-notes-base` → `build-deb` → `validate-release-artifacts` produces:
`changelog.release.md`, `changelog.raw.md`, `changelog.payload.json`,
`changelog.semantic_payload.json`, `changelog.context.json` (required by validate), `release_notes.md`,
`changelog.selection.json`, `release_notes_base.resolution.json`, plus the `.deb`.
`shipped_release.py` maintains the release success ledger used to pick the next release-notes base.

## Legacy/manual path

`make deb` / `make release` → `bin/install_deb.sh [--push]`, which uploads to Nexus APT repos
(`apt.vaulthalla.sh`, `apt.valkyrianlabs.com`) using `UPLOAD_USER` / `UPLOAD_PASS`. It predates the CI pipeline.
Prefer CI.

## Guardrail tests (`tools/release/tests/`)

> **`tools/release/tests/packaging/` has no `__init__.py`, so `unittest discover` (and CI) skips all
> 69 packaging tests.** Run them by module: `verify.sh packaging`. See P0-4 in `production-hardening.md`.


Keep these green: `changelog/test_release_workflow.py`, `test_release_workflow_contract.py`,
`test_ai_semantic_downstream_regression.py`, `test_ai_provider_resolution.py`,
`test_ai_openai_client.py`, `test_ai_openai_compatible_client.py`,
`packaging/test_debian_install_flow_contract.py`, `test_release_artifact_validation.py`,
`test_debian_rules_contract.py`, `test_debian_publication.py`, `test_shipped_release.py`.
Unit tests must never call live OpenAI or local LLM providers; mock them.
Run: `python3 -m unittest discover -s tools/release/tests -p 'test_*.py'` (the same command CI uses; 272 tests as of 1.6.6, about 1s).
