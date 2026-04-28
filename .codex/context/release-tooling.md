# Release Tooling Contract (`tools/release/`)

## Mode

- Release/changelog/build/publication pipeline is in maintenance mode.
- Core implementation is established; current work is hardening and live-runtime validation.

## Canonical Entrypoints

- CLI: `python3 -m tools.release`
- CI packaging action: `.github/actions/package/action.yml`
- CI orchestration workflow: `.github/workflows/release.yml`

`package/action.yml` is the single packaging path; `release.yml` wraps upload/publication/release-asset policy.

## Release Spine (Current)

1. `tools.release check` validates version/release state.
2. Core + web build/test stages run.
3. Changelog resolution runs and writes release artifacts.
4. `build-deb` builds/stages Debian + web artifacts.
5. `validate-release-artifacts` enforces artifact completeness.
6. Workflow uploads staged artifacts.
7. Publication policy resolves from `RELEASE_PUBLISH_REQUIRED=auto|true|false`.
8. `publish-deb` performs Nexus publication (or explicit disabled skip).
9. Tag runs attach deduped staged assets to GitHub Release.

## Changelog Resolution Contract

Provider order is deterministic:

1. hosted OpenAI
2. local OpenAI-compatible endpoint (`RELEASE_LOCAL_LLM_ENABLED=true`)
3. cached local draft (`.changelog_scratch/changelog.draft.md`)
4. manual/no-AI path with stale check against `VERSION`

Primary controls:

- `RELEASE_AI_MODE=auto|openai-only|local-only|disabled`
- `OPENAI_API_KEY`
- `RELEASE_AI_PROFILE_OPENAI`
- `RELEASE_LOCAL_LLM_ENABLED`
- `RELEASE_LOCAL_LLM_PROFILE`
- `RELEASE_LOCAL_LLM_BASE_URL` (explicit override when set)
- `RELEASE_LOCAL_LLM_API_KEY` (optional)

Debian changelog behavior:

- `changelog release` writes evidence artifacts and selected release markdown.
- `changelog ai-draft` writes cached AI draft under `.changelog_scratch/`.
- `changelog ai-release` regenerates cached draft and rewrites Debian top entry from that draft (no manual copy path).
- Generated (`openai`/`local`) paths rewrite a full Debian top entry (header, bullets, maintainer signature, timestamp).
- Manual/no-AI fallback is stale-checked and does not blindly rewrite.

Scratch policy:

- `.changelog_scratch/` is volatile local generation state, not canonical release truth.
- Scratch is expected to clear on upstream version sync/bump transitions.

## Classifier/Triage Architecture Contract

### Deterministic stages must remain deterministic

- Git evidence collection, category assignment, evidence selection, ordering, caps/truncation, and forensic artifacts.
- Deterministic layers should select evidence, not narrate release meaning.

### Model stages own semantic interpretation

- Triage/draft/polish should interpret selected evidence and produce grounded claims.
- Avoid schema pressure that forces path-heavy or caution-heavy boilerplate when evidence is thin.

### Stable design guardrails

- Keep a forensic/debug artifact separate from model-facing semantic payload shape.
- Prevent semantic loss points where possible:
  - payload over-truncation
  - reducing snippet evidence to paths only
  - forcing metadata-centric triage slots
- Keep Debian changelog projection focused on meaningful change claims, not classifier internals.

### Known quality hazards to watch

- Path-heavy summaries (`important files` style output).
- Generic caution spam without real operator risk.
- Diff snippet reduction that strips claim-bearing context.
- Leakage of classifier bookkeeping into final changelog bullets.

## Artifact + Publication Contract

- `build-deb` produces staged Debian and web deliverables.
- `validate-release-artifacts` checks staged output completeness, not just build success.
- `publish-deb` uses:
  - `RELEASE_PUBLISH_MODE=disabled|nexus`
  - `NEXUS_REPO_URL`
  - `NEXUS_USER`
  - `NEXUS_PASS`

Policy expectations:

- Under `auto`, tag refs require publication by default.
- Non-tag refs are optional publication by default.
- Required publication fails on disabled/misconfigured publication or upload failure.

## Deferred Work (Tracked)

- Live install/upgrade validation via published APT/Nexus path.
- Clean-host runtime verification of install/service/nginx behavior.
- Promotion/orchestration beyond current Nexus publication boundary.
