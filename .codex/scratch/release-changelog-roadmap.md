# Release Roadmap Status (Stable Through Phase 12a)

## Current Mode: Stable Release Maintenance

Foundational release-tooling implementation is complete through publication automation.  
Current roadmap focus is live distribution/runtime validation and operational hardening.

## Completed Milestones

- Foundation (pre-10): version/check/sync tooling, changelog context/payload generation, Debian+web build tooling.
- Phase 10a: deterministic changelog path selection (OpenAI -> local gated fallback -> manual stale-check path).
- Phase 10b: release workflow diagnostics and preflight hardening.
- Phase 10c: artifact completeness validation for Debian/web/staged release outputs.
- Phase 11: install/deployment completion (web runtime install, service alignment, conservative nginx path, low-prompt maintainer-script policy, lifecycle hardening).
- Phase 12a: Nexus publication automation restored and policy-gated in release workflow.

## Current Operational Spine

1. Release validation (`tools.release check`) + build/test stages.
2. Canonical package action performs:
   - release preflight
   - changelog resolution (explicit source logs: OpenAI/local/cached/manual)
   - artifact build/staging
   - artifact contract validation
3. Workflow upload of staged artifacts.
4. Publication policy resolution:
   - tag runs: publication required by default (`RELEASE_PUBLISH_REQUIRED=auto`)
   - non-tag runs: publication optional by default
5. Nexus publication via `python3 -m tools.release publish-deb`.
6. Tag-only GitHub release asset attachment using a deduped asset manifest.

## Remaining Work (Phase 12b)

- Live install/upgrade validation through the published APT/Nexus path.
- Clean-host runtime verification for install/service/nginx behavior.
- Operational validation of lifecycle behavior (remove/purge/upgrade) in real install environments.

## Still Deferred (Intentional)

- Promotion/orchestration beyond current Nexus upload boundary.
- Broader installer UX redesign outside current packaging/install contract.

## Local Changelog Workflow Notes

- Volatile changelog scratch state lives under `.changelog_scratch/`.
- `python3 -m tools.release changelog ai-release --ai-profile <profile>` is the deterministic no-copy/paste bridge from AI generation to `/debian/changelog` top-entry rewrite.
