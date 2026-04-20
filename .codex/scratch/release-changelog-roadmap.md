# Release Changelog Roadmap (Post-Buildout State)

This roadmap is now a status-and-hardening tracker.  
The original buildout phases are implemented; current work is integration polish and production hardening.

---

## Implemented Spine (As Of Today)

Completed phases:

- ✅ Phase 1: collector quality foundation
- ✅ Phase 2: deterministic raw renderer
- ✅ Phase 3: CLI changelog draft workflow
- ✅ Phase 4: deterministic AI payload builder
- ✅ Phase 5a: AI draft stage
- ✅ Phase 5b: optional triage stage
- ✅ Phase 5c: optional polish stage
- ✅ Phase 6a: provider seam (hosted OpenAI + OpenAI-compatible)
- ✅ Phase 6b: operator preflight workflow (`changelog ai-check`)
- ✅ Phase 7a: local Debian packaging orchestration (`build-deb`)
- ✅ Phase 7b: web deployable artifact inclusion in release outputs
- ✅ Phase 8: GitHub release workflow with `Production` environment tracking
- [] Phase 9a: Prompt for nginx install in debian package postinst script (deferred from initial buildout to avoid blocking core release flow).
- [] Phase 9b: Nexus/APT publication automation

Current command spine exists end-to-end from local tooling through CI release artifact generation.

---

## Current End-to-End Flow

1. Version guard/sync:
   - `check`, `sync`, `set-version`, `bump`
2. Deterministic release context:
   - git collect -> categorize -> score -> snippets -> context
3. Deterministic outputs:
   - raw markdown draft
   - model-ready payload JSON
4. AI drafting path:
   - payload -> optional triage -> draft -> optional polish -> local markdown
5. Local packaging:
   - `build-deb` (Debian artifacts + Next standalone tarball)
6. CI release path:
   - `.github/workflows/release.yml`
   - canonical package action: `.github/actions/package/action.yml`
   - artifact upload + optional GitHub Release attachment
   - deployment tracked under GitHub `Production` environment

---

## Today’s Key Contract Learnings

1. Debian version validation is upstream-only:
   - `upstream(debian/changelog) == VERSION`
   - revision suffix is not part of canonical comparison.
2. Release workflow web parity requires `build_web` semantics:
   - private icon sync from `~/vaulthalla-web-icons` must happen before web checks.
3. Debian Meson source directory must be `core/` in `debian/rules`.
4. Debian packaging requires explicit staging of non-Meson payloads expected by `debian/install`.
5. Next standalone archive copy must preserve symlinks in CI (pnpm-linked layouts).
6. `environment: Production` in release workflow is mandatory by design.
7. `.github/actions/package` is the canonical CI packaging entrypoint; avoid duplicate packaging logic in workflows.

---

## Current Reality vs Deferred Items

Implemented now:

- deterministic changelog + AI drafting stack
- provider/local-compatible operator workflow
- Debian + web artifact generation
- CI release artifact workflow with Production deployment signal

Still intentionally deferred:

- automatic AI-authored release notes integrated into CI publish flow
- Nexus/APT publication/promotion automation
- stricter CI release gates beyond current validation/test/build coverage
- broader collector quality tuning and rubric hardening

---

## Next Likely Work (Tomorrow-Focused)

1. Deployment-pipeline release-note integration:
   - decide how/when to invoke AI drafting in release workflow
   - keep non-AI fallback deterministic and explicit.
2. CI hardening:
   - tighten failure diagnostics around packaging and artifact validation
   - confirm stable runner assumptions for web/debian steps.
3. Packaging polish:
   - continue validating Debian staging contract against actual package contents.
4. Publication path planning:
   - design safe Nexus/APT publish automation boundary (secrets/env/promotion rules).

---

## Fast Status Summary

The core release/deployment backbone is implemented.  
Remaining work is now integration quality, publication automation, and operational hardening, not foundational architecture buildout.
