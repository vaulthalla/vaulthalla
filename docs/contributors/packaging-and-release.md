# Packaging and Release

## Purpose

This page is the operating guide for contributors touching packaging, changelog generation, artifact validation, or publication in Vaulthalla.

Vaulthalla releases follow one path — no ad-hoc builds, no parallel scripts, no shortcuts. Releases should be reproducible, inspectable, and boring in the best possible way. Every release can be reconstructed from the git tag, the package action, and the published artifacts.

If you are only consuming Vaulthalla releases, you do not need this page. If you are changing packaging tooling, changelog classification, artifact validation, or publication policy, read on.

## Quick Answers

**What is the canonical release path?**
`.github/actions/package/action.yml` is the single packaging entrypoint. It runs preflight checks, resolves the changelog, builds artifacts, and validates them. `.github/workflows/release.yml` wraps it with upload and publication policy.

**What commands matter?**

```bash
python3 -m tools.release check          # validate release state
python3 -m tools.release changelog release   # generate release changelog artifacts
python3 -m tools.release changelog ai-draft  # generate AI-assisted draft (optional)
python3 -m tools.release publish-deb --output-dir <artifact_dir>
```

**What must contributors not bypass?**
Do not bypass `.github/actions/package/action.yml`. Do not invent parallel packaging scripts. Do not treat AI output or `.changelog_scratch/` as canonical. Do not relax artifact validation to make CI green.

**What artifacts prove the release is valid?**
`changelog.payload.json` (deterministic evidence), `changelog.raw.md` (human-reviewable evidence), `changelog.release.md` (selected release text), and the staged Debian/web artifacts that pass `validate-release-artifacts`.

## Release Spine

A release flows through these stages:

1. **State check** — `tools.release check` validates version alignment, changelog presence, and git hygiene.
2. **Build + test** — Core and web build and test stages run. Failure here stops the release.
3. **Changelog resolution** — The package action resolves `changelog.release.md`, `changelog.raw.md`, and `changelog.payload.json` from the classifier pipeline.
4. **Artifact build** — `build-deb` produces staged Debian and web deliverables.
5. **Artifact validation** — `validate-release-artifacts` enforces artifact classes and completeness. A build that succeeds but ships incomplete artifacts is still a failed release.
6. **Upload** — The workflow uploads staged artifacts to the configured destination.
7. **Publication** — `RELEASE_PUBLISH_REQUIRED=auto|true|false` controls whether publication is required (tags) or optional (non-tags).
8. **Debian publication** — `publish-deb` pushes to Nexus or skips in disabled mode.
9. **GitHub Release attachment** — Tag runs attach deduped staged assets to a GitHub Release. Reruns overwrite existing attachments.

## Common Tasks

### Validate release state

```bash
python3 -m tools.release check
```

Validates version alignment, changelog presence, and git hygiene before proceeding with a release.

### Generate release changelog artifacts

```bash
python3 -m tools.release changelog release
```

Runs the classifier pipeline and writes `changelog.payload.json`, `changelog.raw.md`, and `changelog.release.md`.

### Generate an AI-assisted draft

```bash
python3 -m tools.release changelog ai-draft
```

Generates an AI-assisted draft and caches it under `.changelog_scratch/changelog.draft.md`. This is optional scratch state — review before finalizing.

### Finalize AI-assisted Debian changelog

```bash
python3 -m tools.release changelog ai-release
```

Runs AI draft generation then finalizes the Debian changelog from the cached draft. Use this when you want AI-assisted content in the final release.

### Publish Debian artifacts

```bash
python3 -m tools.release publish-deb --output-dir <artifact_dir>  # publish Debian artifacts
```

Pushes staged Debian artifacts to Nexus. Skips silently in `disabled` mode. Fails in `disabled` mode if publication is required (e.g., on a tag).

## Changelog Resolution

The changelog system supports four resolution paths, selected in deterministic priority order:

1. **Hosted OpenAI** — default when `OPENAI_API_KEY` is set and `RELEASE_AI_MODE` is `auto` or `openai-only`.
2. **Local OpenAI-compatible endpoint** — explicitly gated by `RELEASE_LOCAL_LLM_ENABLED=true`.
3. **Cached local draft** — `.changelog_scratch/changelog.draft.md`.
4. **Manual/no-AI fallback** — stale-checked, logged as fallback, does not blindly rewrite.

Local LLM is **never** the fallback. It must be explicitly enabled. If no provider is available or configured, the manual path runs a stale check and logs the fallback.

### CLI commands

```bash
# Write release evidence artifacts and the selected release markdown.
python3 -m tools.release changelog release

# Write a cached draft artifact under .changelog_scratch by default.
python3 -m tools.release changelog ai-draft

# Run AI draft generation then finalize Debian changelog from the cached draft.
python3 -m tools.release changelog ai-release
```

### Environment variables

| Variable | Values | Purpose |
|---|---|---|
| `RELEASE_AI_MODE` | `auto` (default) \| `openai-only` \| `local-only` \| `disabled` | Controls provider selection |
| `RELEASE_LOCAL_LLM_ENABLED` | `true` \| `false` (default) | Explicitly gates local LLM usage |
| `RELEASE_LOCAL_LLM_BASE_URL` | Any URL | Overrides local profile `base_url` (logged when set) |
| `RELEASE_DEBIAN_DISTRIBUTION` | e.g. `unstable` | Distribution token for Debian changelog |
| `RELEASE_DEBIAN_URGENCY` | e.g. `low` | Urgency token for Debian changelog |

### Local LLM and AI-Assisted Changelog Generation

Local LLM support is optional and opt-in behind `RELEASE_LOCAL_LLM_ENABLED=true`. When enabled, the classifier pipeline routes through a local OpenAI-compatible endpoint specified by `RELEASE_LOCAL_LLM_BASE_URL` (or the local profile's `base_url` if the env var is not set).

AI-assisted output is grounded in deterministic evidence artifacts, but generated text must still be reviewed before it becomes release truth. The scratch directory (`.changelog_scratch/`) holds volatile cache and generation state — it is not canonical truth. Canonical output lives in the committed changelog files and the Debian changelog.

When `RELEASE_AI_MODE=local-only`, the system skips hosted OpenAI entirely and uses the local endpoint or falls through to cached/manual paths. When `RELEASE_AI_MODE=disabled`, all AI paths are skipped and the manual path runs with stale checking.

## Debian Changelog Contract

Generated changelog paths (hosted OpenAI or local endpoint) refresh the top `debian/changelog` entry as a full Debian record:

- Package name + full Debian version
- Distribution token (from `--debian-distribution` flag, `RELEASE_DEBIAN_DISTRIBUTION` env, or existing top-entry fallback)
- Urgency token (from `--debian-urgency` flag, `RELEASE_DEBIAN_URGENCY` env, or existing top-entry fallback)
- Summary bullet body
- Maintainer signature line
- RFC-2822 timestamp

Distribution and urgency resolution is explicit and validated in this order:

1. CLI flags: `--debian-distribution`, `--debian-urgency`
2. Environment variables: `RELEASE_DEBIAN_DISTRIBUTION`, `RELEASE_DEBIAN_URGENCY`
3. Existing top-entry values (final fallback)

The manual/no-AI path (`debian/changelog` fallback) is stale-checked against the current `VERSION`. If stale, it logs the fallback explicitly and does not rewrite the changelog. If current, it may be used as the manual release source without blind regeneration. This is intentional — the manual path exists to protect against blind overwrites, not to generate new content.

## Artifact and Runtime Validation

`build-deb` produces staged Debian and web deliverables. `validate-release-artifacts` enforces artifact classes and completeness checks.

This contract validates more than "build completed." It checks shipped output completeness against current install/deploy expectations:

- **Debian package payload checks** — Verifies the `.deb` contains the expected file layout, maintainerscripts, control metadata, and binary payloads.
- **Web archive runtime layout checks** — Verifies the web archive contains the expected runtime files under `/usr/share/vaulthalla-web` and that service/unit files reference correct paths.

If validation fails, the release does not proceed to upload. This is a hard gate.

## Publication and GitHub Release Attachments

### Publication policy

Publication is controlled by `RELEASE_PUBLISH_MODE`:

- `disabled` — skip all publication steps. Optional runs silently skip. Required runs fail with a clear error.
- `nexus` — push to Nexus using `NEXUS_REPO_URL`, `NEXUS_USER`, `NEXUS_PASS`.

Publish command:

```bash
python3 -m tools.release publish-deb --output-dir <artifact_dir> [--require-enabled]
```

Behavior:

- **Optional runs** (non-tag refs, `RELEASE_PUBLISH_REQUIRED=false` or `auto` on non-tags): skip silently when disabled.
- **Required runs** (tag refs, `RELEASE_PUBLISH_REQUIRED=true` or `auto` on tags): fail if publication is disabled, misconfigured, or upload fails. The `--require-enabled` flag enforces this explicitly.

### GitHub Release attachments

Tag runs attach deduped staged assets to a GitHub Release. The workflow:

- Prepares a deduplicated asset list from staged artifacts.
- Uses `softprops/action-gh-release` with `overwrite_files: true` so reruns overwrite existing attachments rather than failing on duplicates.
- Skips attachment entirely when not on a tag ref.

## Classifier Pipeline Overview

The changelog classifier pipeline transforms raw git history into structured release notes through a deterministic, multi-stage flow:

> git evidence → categorized context → bounded payload → raw markdown → final changelog

Contributors usually only need this section when modifying changelog classification, scoring, payload projection, or AI prompt plumbing.

| Stage | Module | Responsibility |
|---|---|---|
| Evidence collection | `git_collect.py` | Gathers commits (sha/subject/body), per-commit file lists, numstat, aggregate file stats, and full file patch text. |
| Classification | `categorize.py` | Maps file paths to fixed categories (`debian`/`tools`/`deploy`/`web`/`core`/`meta`), subscopes, flags, and path-derived themes. |
| Ranking | `scoring.py` | Computes file scores (churn + path/flag heuristics) and hunk scores (keyword + line-count heuristics). |
| Hunk selection | `snippets.py` | Extracts top hunks from top files via score ranking, attaches generic reasons. |
| Context assembly | `context_builder.py` | Assembles per-category `CategoryContext` and finalizes `ReleaseContext`. |
| AI payload | `payload.py` | Projects `ReleaseContext` into capped model-facing JSON with truncation metadata and signal strength. |
| Raw renderer | `render_raw.py` | Projects `ReleaseContext` into deterministic human-reviewable markdown. |
| Workflow orchestration | `release_workflow.py` | Selects final release source (openai/local/cached-draft/manual), writes selected changelog, and refreshes `debian/changelog`. |

The deterministic stack (collection through context assembly) is strong: category order, file/snippet selection, and payload truncation are stable and reproducible. The AI-assisted stages consume this structured output after `payload.py` has reduced it into a bounded model-facing artifact.

## Contributor Rules of Engagement

These rules keep releases reliable and inspectable:

1. **Do not bypass `.github/actions/package/action.yml`.** If a new packaging step or validation check is needed, add it to the action and update this document. Ad-hoc build scripts create unreproducible releases.

2. **Do not invent parallel packaging scripts.** The CI action is the single packaging path. Any divergence between local and CI behavior is a bug — fix the alignment, not the documentation.

3. **Do not treat AI output or `.changelog_scratch/` as canonical.** Scratch artifacts are volatile local generation state. They are cleared on version transitions. AI-assisted output is grounded in deterministic evidence artifacts, but generated text must still be reviewed before it becomes release truth.

4. **Do not relax artifact validation to make CI green.** `validate-release-artifacts` is a hard gate. A successful build with incomplete or mislaid artifacts is still a failed release. Fix the root cause.

5. **Changes to scoring/payload/projection require fixture and artifact updates.** If you modify `scoring.py`, `payload.py`, `triage.py`, or any module that changes data flowing through the pipeline, update the corresponding fixtures in `tools/release/tests/changelog/fixtures/` and any stale scratch artifacts.

6. **Prefer small, auditable changes over broad release-flow rewrites.** The release system is sensitive. A focused change to one module with updated fixtures is safer and more reviewable than a multi-module reshuffle.

## Deferred Work

Two areas are intentionally out of scope for the current release system:

1. **Phase 12b clean-host live APT/runtime validation** — Full integration testing on a clean Debian host (install, upgrade, purge, FUSE mount validation) is deferred. This would add significant CI time and infrastructure requirements without changing the packaging output.

2. **Broader repository promotion/orchestration** — Repository mirroring, APT repository management, and multi-distribution promotion beyond the current Nexus upload boundary are deferred. The current system uploads to Nexus; broader distribution strategy is a separate concern.

## Quick Command Reference

```bash
# Validate release state (version, changelog, git hygiene)
python3 -m tools.release check

# Write release evidence artifacts and the selected release markdown
python3 -m tools.release changelog release

# Write a cached draft artifact under .changelog_scratch
python3 -m tools.release changelog ai-draft

# Run AI draft generation then finalize Debian changelog from cached draft
python3 -m tools.release changelog ai-release

# Publish deb artifacts to Nexus (or skip in disabled mode)
python3 -m tools.release publish-deb --output-dir <artifact_dir>
```