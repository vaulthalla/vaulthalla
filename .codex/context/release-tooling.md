# Release Tooling Map (`tools/release/`)

Current operator map for release generation, packaging, install contract checks, and publication.

## Current Scope

`tools/release` currently covers:

1. Version authority and drift enforcement (`check/sync/set-version/bump`).
2. Deterministic changelog context + payload generation.
3. Release changelog path selection with deterministic AI/local/manual fallback.
4. Debian + web artifact build and contract validation.
5. Debian publication to Nexus (env/secret-gated).

## Canonical Entrypoints

- CLI entrypoint: `python3 -m tools.release`
- Canonical CI packaging action: `.github/actions/package/action.yml`
- Canonical release workflow: `.github/workflows/release.yml`

`package/action.yml` is still the single packaging entrypoint in CI.  
`release.yml` orchestrates publish/upload/release attachment around that action.

## Command Surface (Current)

```bash
python3 -m tools.release check
python3 -m tools.release sync [--dry-run] [--debian-revision N]
python3 -m tools.release set-version X.Y.Z [--dry-run] [--debian-revision N]
python3 -m tools.release bump {major|minor|patch} [--dry-run] [--debian-revision N]

python3 -m tools.release changelog release \
  --output release/changelog.release.md \
  --raw-output release/changelog.raw.md \
  --payload-output release/changelog.payload.json

python3 -m tools.release build-deb --output-dir release [--dry-run]
python3 -m tools.release validate-release-artifacts --output-dir release [--skip-changelog]
python3 -m tools.release publish-deb --output-dir release [--mode disabled|nexus] [--dry-run]
```

## Current Release Spine

1. Release state validation (`tools.release check`).
2. Core + web build/test workflow steps.
3. Package action preflight checks.
4. Changelog release stage:
   - writes deterministic evidence (`changelog.raw.md`, `changelog.payload.json`)
   - resolves final changelog path (`changelog.release.md`)
5. Artifact build (`build-deb`) + contract validation (`validate-release-artifacts`).
6. Workflow artifact upload + GitHub release attachment (tags).
7. Publication stage (`publish-deb`) to Nexus when enabled.

## Changelog Path Selection (Phase 10a Contract)

Configured by:

- `RELEASE_AI_MODE=auto|openai-only|local-only|disabled`
- `OPENAI_API_KEY`
- `RELEASE_AI_PROFILE_OPENAI`
- `RELEASE_LOCAL_LLM_ENABLED=true|false`
- `RELEASE_LOCAL_LLM_PROFILE`
- `RELEASE_LOCAL_LLM_BASE_URL`
- `RELEASE_LOCAL_LLM_API_KEY` (optional)

Deterministic order:

- `auto`: hosted OpenAI -> local OpenAI-compatible -> manual/no-AI
- `openai-only`: hosted OpenAI -> manual/no-AI
- `local-only`: local OpenAI-compatible -> manual/no-AI
- `disabled`: manual/no-AI only

Important behavior:

- Local fallback requires explicit `RELEASE_LOCAL_LLM_ENABLED=true`; base URL presence alone does not activate local.
- `RELEASE_LOCAL_LLM_BASE_URL` explicitly overrides local profile `base_url` and logs that override.
- Manual path enforces a stale check against `VERSION` and fails when `debian/changelog` is missing/stale.

## Packaging and Artifact Contract (Phase 10b/10c + 11)

`build-deb`:

- runs web install/build (`pnpm install`, `pnpm build`)
- creates standalone web deployable archive
- runs Debian build
- copies Debian artifacts into release output dir

`validate-release-artifacts`:

- validates required artifact classes exist (`.deb`, web archive, changelog artifacts unless skipped)
- validates Debian package payload completeness (binary/unit/config/docs/web/nginx expected paths)
- validates web archive completeness (`server.js`, static payload, safe archive paths)

## Install Path and Lifecycle (Phase 11 + Lifecycle Hardening)

High-level package behavior now:

- Installs web runtime under `/usr/share/vaulthalla-web`.
- Installs and enables `vaulthalla-web.service`.
- Uses debconf templates (`init-db`, `superadmin-uid`, `configure-nginx`).
- Performs conservative nginx integration (safe-skip on non-greenfield conditions).
- Maintainer lifecycle scripts explicitly separate remove vs purge boundaries:
  - remove: conservative stop/disable + runtime seed cleanup
  - purge: debconf purge + system/state cleanup within defined package-owned boundaries

Details remain documented in `debian/README.Debian` and script contracts/tests.

## Publication Boundary (Phase 12a)

Publication command:

- `python3 -m tools.release publish-deb --output-dir <artifact_dir>`

Mode/config:

- `RELEASE_PUBLISH_MODE=disabled|nexus`
- `NEXUS_REPO_URL`
- `NEXUS_USER`
- `NEXUS_PASS`

Behavior:

- `disabled`: explicit skip with diagnostic output.
- `nexus`: validates config + selects sorted `*.deb` artifacts + uploads each to Nexus via curl; fails clearly on missing config/artifacts/upload error.

No canonical runner-local env sourcing (`source ~/runner_local.env`) is used.

## Deferred / Next

Still intentionally deferred:

1. Phase 12b live distribution validation against the real published APT path (install/upgrade on clean hosts).
2. Any broader publication/promotion orchestration beyond current Nexus upload boundary.
3. Automatic GitHub release-body authoring from AI changelog output.
