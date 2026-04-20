# Release Changelog Roadmap (Integration, Packaging & Distribution Phase)

The release/changelog control plane is now built and operational.  
Remaining work is focused on CI integration, installation completeness, and distribution automation.

---

## Completed Foundation

Implemented and working:

- ✅ Deterministic changelog collection, categorization, scoring, and rendering
- ✅ AI drafting pipeline:
  - triage
  - draft
  - polish
- ✅ Hosted OpenAI + OpenAI-compatible provider seam
- ✅ AI profile/config control plane
- ✅ Stage-aware execution:
  - model selection
  - reasoning effort
  - structured mode
  - temperature
  - max output tokens
  - dynamic token budgeting
- ✅ Provider capability handling and structured fallback behavior
- ✅ Prompt tightening and runtime validation hardening
- ✅ Local Debian packaging orchestration (`build-deb`)
- ✅ Web artifact inclusion in release outputs
- ✅ GitHub release workflow with `Production` environment tracking

The changelog/release backbone is complete.  
The remaining roadmap is about integrating it cleanly into deployment and installation workflows.

---

## Current Release Spine

1. Version guard/sync
   - `check`, `sync`, `set-version`, `bump`

2. Deterministic release context
   - git collect → categorize → score → snippets → context

3. Changelog outputs
   - raw markdown draft
   - AI-ready payload JSON
   - AI-authored markdown draft

4. Packaging
   - Debian package build
   - web standalone artifact generation

5. CI release path
   - package action
   - artifact upload
   - GitHub release attachment
   - Production deployment tracking

---

## Phase 10: AI Release Integration & CI Hardening

**Goal:** Integrate changelog AI into the release workflow cleanly and make CI behavior production-stable.

### 10a: AI Draft Integration in Release Workflow

- Run changelog AI automation during release workflow when required AI secrets/config are present in the `Production` environment
- Use deterministic fallback when AI is unavailable, disabled, or fails
- Keep release pipeline behavior explicit:
  - AI path when configured
  - deterministic path when not

### 10b: Release Workflow Hardening

- Tighten failure diagnostics for:
  - changelog generation
  - packaging
  - artifact validation
  - AI stage failures
- Ensure package action remains the canonical packaging entrypoint
- Verify stable assumptions for release runners and environment inputs

### 10c: Packaging Validation

- Confirm Debian package contents match install expectations
- Confirm web artifact contents are complete and correctly staged
- Validate release assets before publication

---

## Phase 11: Install & Deployment Completion

**Goal:** Finish the actual install experience so packaged deployment is end-to-end, not just artifact-complete.

### 11a: Web Install Completion

- Ensure Debian install fully deploys required web assets and runtime components
- Verify web/CLI deployment is actually complete during install
- Eliminate any remaining gap between built web artifacts and installed web experience

### 11b: Nginx / Reverse Proxy Install Flow

- Add install-time nginx integration for the web/CLI surface
- Support:
  - prompt-based setup when appropriate
  - safe auto-configuration when nginx is already installed and active on port 80/443
- Do not overwrite or break existing reverse proxy setups
- Detect and respect systems already running another proxy/configuration

### 11c: Install Path Validation

- Test fresh install end-to-end on a clean target
- Validate:
  - package install
  - service setup
  - web deployment
  - proxy integration
  - post-install usability

---

## Phase 12: Publication & Distribution Automation

**Goal:** Restore and complete repository publication so installs can be tested from the real package distribution path.

### 12a: Nexus/APT Publication Automation

- Restore Nexus/APT publication flow
- Automate upload/promotion of Debian packages to the Nexus repository
- Re-establish install testing through the real APT path
- Define safe publication boundaries, credentials, and environment handling

### 12b: Distribution Validation

- Verify full install works from published APT repository
- Validate upgrade/install flow against the live repo, not just local artifacts
- Confirm package metadata, dependency handling, and install path behave correctly

---

## Current Priorities

1. Integrate AI changelog generation into the release workflow when OpenAI credentials/config are present
2. Harden release workflow diagnostics and validation
3. Finish Debian install path for web deployment and nginx integration
4. Restore Nexus/APT publication so real install testing is possible

---

## Fast Status Summary

The release/changelog system itself is built.

The next milestone is turning it into a fully integrated distribution pipeline:
- AI-assisted release generation in CI
- complete Debian/web installation
- nginx/reverse proxy install flow
- restored Nexus/APT publication and live install validation
- 