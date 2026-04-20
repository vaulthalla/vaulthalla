# Release Roadmap Status (Post Phase 12a)

This is the condensed status snapshot for release/changelog/packaging/install/publication work.

## Completed Through Current Implementation

## Foundation (pre-Phase 10)

- Deterministic changelog context/payload generation.
- AI changelog drafting pipeline (triage/draft/polish) with provider abstraction.
- Version synchronization and release-state validation tooling.
- Debian + web artifact build tooling.

## Phase 10a - AI release integration

- `changelog release` integrated into canonical package action flow.
- Deterministic provider order implemented:
  - Hosted OpenAI
  - Local OpenAI-compatible
  - Manual/no-AI
- Local fallback explicitly gated (`RELEASE_LOCAL_LLM_ENABLED=true`).
- `RELEASE_LOCAL_LLM_BASE_URL` override implemented and logged.
- Manual path stale/missing changelog validation against `VERSION`.

## Phase 10b - workflow hardening

- Clear failure boundaries for changelog, packaging, and artifact validation stages.
- Runner/env preflight checks added.
- Package action kept canonical for packaging logic.

## Phase 10c - packaging validation

- Artifact completeness checks expanded beyond "build succeeded":
  - Debian package payload contract
  - web standalone archive contract
  - staged release artifact presence checks

## Phase 11 - install/deployment completion

- Web runtime deployment completed in Debian package payload.
- Web systemd unit alignment completed.
- Conservative nginx install-time integration implemented.
- Debconf prompt path (`debian/templates`) reused and extended.
- Maintainer lifecycle semantics hardened (install/remove/purge behavior clarified and tested).

## Phase 12a - publication automation (implemented)

- Nexus publication path restored via `python3 -m tools.release publish-deb`.
- Explicit publication gating/config:
  - `RELEASE_PUBLISH_MODE=disabled|nexus`
  - `NEXUS_REPO_URL`, `NEXUS_USER`, `NEXUS_PASS`
- Deterministic `.deb` artifact selection for upload.
- Clear skip/fail diagnostics for publication stage.
- Release workflow now includes explicit post-package publication step.

## Current Release Spine

1. Release state validation (`tools.release check`).
2. Core/web build and tests.
3. Canonical package action:
   - preflight
   - changelog resolution (AI/manual fallback)
   - build (`build-deb`)
   - artifact contract validation
4. Workflow artifact upload + tag release attachment.
5. Publication step (`publish-deb`) with explicit mode/config gating.

## Remaining Work (Not Yet Implemented)

## Phase 12b - distribution runtime validation

Still pending:

- Install/upgrade validation against the real published APT/Nexus path.
- Clean-host live tests for package dependency resolution and runtime behavior.
- End-to-end validation that remove/purge lifecycle behaves as expected in live environments.

## Deferred Beyond Current Scope

- Publication promotion/orchestration beyond current Nexus upload boundary.
- Unrelated installer UX expansion or packaging redesign.
- Non-essential changelog polish automation beyond current release contract.
