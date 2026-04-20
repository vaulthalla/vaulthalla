# Release Tooling Map (`tools/release/`)

This is the current architecture and status map for release/version/changelog automation in this repository.

## Current Scope

`tools/release` currently does three real jobs:

1. Version-state management (authoritative `VERSION` sync and drift validation).
2. Changelog generation pipeline (collect/categorize/score/snippet/context, raw draft rendering, deterministic AI payload projection, and local single-pass AI draft generation).
3. Local Debian packaging build orchestration (`build-deb`), including artifact collection into a deterministic output directory.

AI changelog generation now supports a staged flow with optional triage and polish passes.

## Package Layout (Current Code)

Top level:

- `tools/release/__main__.py`: `python -m tools.release` entrypoint.
- `tools/release/cli.py`: CLI parser + version commands + changelog `draft`/`payload`/`ai-draft` flows.

Version system:

- `tools/release/version/models.py`: `Version` semver model (`X.Y.Z`, bump helpers).
- `tools/release/version/adapters/`:
  - `version_file.py`: read/write `VERSION`.
  - `meson.py`: read/write version in `core/meson.build`.
  - `package_json.py`: read/write version in `web/package.json`.
  - `debian.py`: read/write Debian changelog header (`X.Y.Z-N`).
- `tools/release/version/validate.py`:
  - path model (`ReleasePaths`)
  - read-state model (`VersionReadResult`)
  - issue/state models (`ValidationIssue`, `ReleaseState`)
  - drift/structural validation and requirement helpers.
- `tools/release/version/sync.py`:
  - `resolve_debian_revision(...)`
  - `apply_version_update(...)` write orchestration.

Changelog system:

- `tools/release/changelog/models.py`: immutable data shapes (`CommitInfo`, `FileChange`, `DiffSnippet`, `CategoryContext`, `ReleaseContext`).
- `tools/release/changelog/git_collect.py`: raw git collection only (`get_latest_tag`, `get_commits_since_tag`, `get_release_file_stats`, `get_file_patch`, etc.).
- `tools/release/changelog/categorize.py`: path normalization, category assignment, subscopes, flags/themes.
- `tools/release/changelog/scoring.py`: file and hunk scoring heuristics.
- `tools/release/changelog/snippets.py`: patch hunk splitting + top snippet selection.
- `tools/release/changelog/context_builder.py`: orchestration into `ReleaseContext` (commit/file stats + snippets).
- `tools/release/changelog/render_raw.py`: raw markdown/debug/json rendering.
- `tools/release/changelog/payload.py`: deterministic model-ready payload builder (`schema_version`, bounded evidence, truncation metadata).
- `tools/release/changelog/ai/`:
  - `config.py`: internal provider/model defaults and typed provider config surface.
  - `contracts/`: typed schema-bound contracts (`draft.py`, `triage.py`, `polish.py`).
  - `prompts/`: prompt builders per stage (`draft.py`, `triage.py`, `polish.py`).
  - `providers/`: transport seam (`openai.py`, `openai_compatible.py`) plus preflight/model discovery.
  - `stages/`: stage orchestration (`draft.py`, `triage.py`, `polish.py`).
  - `render/markdown.py`: local markdown rendering from typed stage outputs.

Debug surface:

- `tools/release/debug/release_context.py`: local harness to inspect release context and dump JSON.

## Command Surface (Current)

```bash
python3 -m tools.release check
python3 -m tools.release sync [--dry-run] [--debian-revision N]
python3 -m tools.release set-version X.Y.Z [--dry-run] [--debian-revision N]
python3 -m tools.release bump {major|minor|patch} [--dry-run] [--debian-revision N]
python3 -m tools.release build-deb [--output-dir PATH] [--dry-run]
python3 -m tools.release changelog draft [--format raw|json] [--since-tag TAG] [--output PATH]
python3 -m tools.release changelog payload [--since-tag TAG] [--output PATH]
python3 -m tools.release changelog ai-check [--provider openai|openai-compatible] [--base-url URL] [--model MODEL]
python3 -m tools.release changelog ai-draft [--since-tag TAG] [--output PATH] [--save-json PATH] [--model MODEL] [--provider openai|openai-compatible] [--base-url URL] [--use-triage] [--save-triage-json PATH] [--polish] [--save-polish-json PATH]
```

Debug harness:

```bash
python3 -m tools.release.debug.release_context [--repo-root PATH] [--json]
```

## Canonical Version Authority + Managed Files

Canonical version source: top-level `VERSION`.

Managed files validated/synced against `VERSION`:

- `core/meson.build`
- `web/package.json`
- `debian/changelog` (upstream must match; Debian revision is preserved/overridden by sync args)

`check` behavior from `version/validate.py`:

- detects structural issues (missing files, read failures, invalid parse)
- detects drift mismatches
- emits actionable sync guidance (`python -m tools.release sync`)

## Debian Version Handling

- `VERSION` stores upstream semver only (for example, `0.29.0`).
- `debian/changelog` stores full Debian version (for example, `0.29.0-1`).
- Validation compares `VERSION` to `debian/changelog` upstream only (revision suffix is ignored for match checks).
- `sync` and `bump` preserve existing Debian revision unless an explicit revision override is provided.

## Debian Build Orchestration

`python -m tools.release build-deb` performs a local, operator-driven Debian build flow:

1. Requires synced release state (`VERSION`, `core/meson.build`, `web/package.json`, `debian/changelog` must align).
2. Validates Debian packaging prerequisites (`debian/control`, `debian/rules`, `debian/source/format`).
3. Runs `dpkg-buildpackage -us -uc -b`.
4. Builds the Next.js standalone deployable web artifact and archives it to:
   - `release/<package>-web_<version>_next-standalone.tar.gz`
5. Copies generated Debian artifacts (for the current package/version) into `release/` by default.
6. Writes `release/build-deb.log` with captured web install/build + Debian build stdout/stderr.

This command is local-only and does not publish artifacts, create tags, or invoke CI.

## GitHub Release Workflow (Phase 8)

CI release workflow:

- `.github/workflows/release.yml`
- triggers:
  - `workflow_dispatch`
  - tag push (`v*`)
- release job uses:
  - `environment: Production` (mandatory deployment tracking)
- validation steps include:
  - `python3 -m tools.release check`
  - core build + unit tests via existing local actions
  - release-tooling unit tests
  - web checks
- canonical packaging path in CI:
  - `/.github/actions/package/action.yml`
  - delegates to `python -m tools.release build-deb --output-dir release`

Workflow outputs include:

- Debian artifacts (`.deb`, `.changes`, `.buildinfo`, etc.)
- Next.js standalone deployable archive from release output
- changelog evidence files (`changelog.raw.md`, `changelog.payload.json`)

## Current Changelog Collection Pipeline

Implemented flow in `changelog/context_builder.py`:

1. Resolve `previous_tag` via `git describe --tags --abbrev=0` when not provided.
2. Collect commits in range (`previous_tag..HEAD` or `HEAD`) via `git log`.
3. Gather per-file aggregate stats via `git diff --numstat`.
4. Build category contexts:
   - path -> category (`categorize.py`)
   - subscopes + flags + themes
   - file score ranking (`scoring.py`)
5. Extract snippets:
   - fetch patch per candidate file
   - split hunks + score hunks
   - keep top hunks per file/category (`snippets.py`)
6. Return `ReleaseContext` with final categories including snippets.

Raw renderers in `render_raw.py` provide:

- simple markdown-ish changelog block (`render_release_changelog`)
- human debug text (`render_debug_context`)
- JSON payload (`render_debug_json`)

## Current Debug Flow

`python -m tools.release.debug.release_context --json`:

1. Reads canonical version from `VERSION`.
2. Calls `build_release_context(version=..., repo_root=...)`.
3. Prints human debug sections by category.
4. Optionally emits full JSON (`dataclasses.asdict`) payload.

This is currently the best introspection path for tuning collector quality.

## Known Gaps / Limitations (Current)

1. Raw renderer remains intentionally conservative and evidence-bound (not a polished publication format).
2. Categorization/scoring/snippet logic is heuristic and still needs broader fixture coverage for edge-case ranges.
3. AI stages are available, but CI release flow intentionally does not hard-require AI credentials/secrets.
4. Release publishing beyond GitHub artifacts/releases (APT/Nexus promotion channels) is still deferred.

## Intended Progression Toward AI-Assisted Changelog Generation

Planned progression from current state:

1. Continue collector quality and determinism tuning.
2. Improve/validate renderer quality using representative fixtures.
3. Expand from single-pass mini drafting to staged AI flow (Phase 5b/5c).
4. Add optional refinement passes only after baseline deterministic quality is stable.

Roadmap tracking file:

- `.codex/scratch/release-changelog-roadmap.md`

## Local OpenAI-Compatible Workflow (vMLX)

For local dogfood on vMLX-compatible endpoints:

- Base URL format: `http://localhost:8888/v1`
- Example local model names: `Qwen3.5-122B`, `Gemma-4-31B`
- Local compatible mode can run without `OPENAI_API_KEY`

Preflight provider/model connectivity before generation:

```bash
python3 -m tools.release changelog ai-check \
  --provider openai-compatible \
  --base-url http://localhost:8888/v1 \
  --model Qwen3.5-122B
```

Then run generation with the same provider settings:

```bash
python3 -m tools.release changelog ai-draft \
  --provider openai-compatible \
  --base-url http://localhost:8888/v1 \
  --model Qwen3.5-122B
```
