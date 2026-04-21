# Debian Install and CLI Tools Staging Plan

## Goal

Refactor Vaulthalla’s Debian install behavior so that:

- `apt install vaulthalla` remains clean, conservative, and low-prompt
- common-case installs still “just work” where safe
- optional integrations like PostgreSQL bootstrap, nginx configuration, and certbot orchestration move toward explicit CLI-driven flows
- packaging and CLI responsibilities are clearly separated
- release contract tests enforce the intended install behavior going forward

This plan intentionally splits Debian package concerns from richer system orchestration concerns.

---

## Design Direction

### Debian package responsibilities
The Debian package should:

- install binaries, configs, service files, packaged assets, and required runtime directories
- install recommended helper packages through standard package metadata only
- perform only conservative, deterministic, safe post-install actions
- avoid broad system mutation when intent is ambiguous
- avoid heavy interactive prompting in the common path
- never rely on dev install scripts or shell-session voodoo for behavior

### CLI responsibilities
The Vaulthalla CLI should become the explicit control plane for optional/admin actions such as:

- local DB bootstrap
- nginx deployment/configuration
- certbot orchestration
- optional teardown/reset flows
- future diagnostics / “doctor” style checks

This creates a clean boundary:

- package manager provisions software
- Vaulthalla CLI provisions Vaulthalla integrations

---

## Phase 1 — Debian install contract stabilization

### Objective
Finish stabilizing the Debian package so install/remove/purge behavior is correct and runtime parity is trustworthy.

### Scope
- Ensure all packaged runtime assets are installed from canonical sources
- Ensure packaged paths match runtime expectations
- Ensure maintainer scripts align with safe setup/teardown semantics
- Remove drift between `/bin` production intent and `/debian` packaging behavior

### Expected work
- Audit and repair:
  - `debian/install`
  - `debian/rules`
  - `debian/postinst`
  - `debian/prerm`
  - `debian/postrm`
  - `debian/control`
- Verify packaged install contains:
  - SQL migration/deploy assets
  - required `/usr/share/vaulthalla/*` payloads
  - expected `/etc/vaulthalla/*` config files
  - required `/var/lib/vaulthalla` and `/run/vaulthalla` directories
- Ensure remove vs purge semantics remain conservative and correct
- Ensure contract tests fail CI when runtime assets are missing

### Exit criteria
- Fresh package install no longer fails due to missing runtime assets
- Package payload matches runtime expectations
- Release contract tests cover core package payload/install contract

---

## Phase 2 — Debconf and prompt policy cleanup

### Objective
Reduce or eliminate routine package-time prompting and move toward safe, intelligent defaults.

### Policy direction
Routine interactive prompts during `apt install` are undesirable unless there is a meaningful conflict that actually requires user intent.

### Scope
- Audit current `debian/templates`, `debian/config`, and debconf wiring
- Decide whether templates remain for conflict-only prompting or are removed entirely
- Eliminate dead template scaffolding if not truly needed

### Decisions to enforce
- `superadmin-uid` prompt should be removed from package install flow
- super-admin CLI ownership should be claimed on first CLI invocation instead
- `init-db` and `configure-nginx` should not require routine prompts in the common path
- if prompting remains at all, it should be conflict-driven only

### Expected work
- Audit:
  - `debian/templates`
  - `debian/config`
  - `debian/postinst`
- Remove dead or misleading debconf behavior
- Update docs to reflect final policy
- Ensure noninteractive install does not hang or behave unpredictably

### Exit criteria
- No unnecessary routine package prompts
- No dead debconf/template scaffolding
- Prompt behavior, if retained, is deliberate and conflict-only

---

## Phase 3 — PostgreSQL and nginx package policy shift

### Objective
Treat PostgreSQL and nginx as optional integrations rather than hard package-time assumptions.

### Policy direction
- `postgresql` should move from `Depends` to `Recommends`
- `nginx` should also be optional/recommended if package metadata still implies stronger coupling
- `--no-install-recommends` should naturally produce a lean install without DB/nginx side effects

### Scope
- Align package metadata with actual runtime/install philosophy
- Make postinst key off actual installed state, not assumed intent
- Stop treating local DB/proxy setup as mandatory package behavior

### Expected work
- Update `debian/control`
- Update `postinst` logic:
  - if PostgreSQL is present and safe to bootstrap locally, bootstrap can proceed
  - if absent, skip and report next steps
  - if nginx is present and safe to configure, configure conservatively
  - if absent or conflicting, skip and report
- Ensure no package install path tries to install packages from maintainer scripts

### Desired install behavior
#### `apt install vaulthalla`
- installs Vaulthalla
- usually installs recommended postgres/nginx
- performs smart, safe setup when conditions are satisfied
- skips risky or unavailable integrations and reports what happened

#### `apt install --no-install-recommends vaulthalla`
- installs Vaulthalla only
- leaves postgres/nginx absent unless already present
- skips corresponding smart setup unless system state still makes it valid
- reports next steps for manual follow-up

### Exit criteria
- Package metadata matches real install philosophy
- Lean install path works cleanly
- Remote-DB and custom-proxy users are no longer fighting hard package assumptions

---

## Phase 4 — Super-admin bootstrap ownership cleanup

### Objective
Move initial super-admin identity bootstrap fully out of Debian install logic.

### Policy direction
The package install should not ask the user for a Linux UID. That is ugly, fragile, and not aligned with how Vaulthalla CLI already behaves.

### Final behavior
- package install does not prompt for super-admin UID
- first Linux user to invoke `vh` or `vaulthalla` claims initial CLI super-admin ownership
- install completion messaging explains this clearly
- future users still require explicit Linux UID mapping in Vaulthalla where appropriate

### Expected work
- remove package-time superadmin prompt/logic
- ensure install summary or postinst messaging explains first-use bootstrap
- document the one-time nature of CLI super-admin claim

### Exit criteria
- No UID prompt in package install
- CLI first-use claim flow is the only initial ownership path
- Docs/install messages clearly explain next step

---

## Phase 5 — Minimal CLI integration commands

### Objective
Introduce explicit CLI commands for optional integrations that Debian should not fully own.

### Initial target commands
- `vh setup db`
- `vh setup nginx`
- `vh teardown nginx`
- optionally later:
  - `vh teardown db`
  - `vh doctor`
  - `vh setup status`

### Important boundary
These commands should manage Vaulthalla-specific configuration/bootstrap tasks, not recklessly uninstall arbitrary system packages unless that behavior is very explicit and intentional.

### Recommended semantics
#### `vh setup db`
- bootstrap Vaulthalla DB/user/schema against local PostgreSQL
- optionally later support remote DB bootstrap where appropriate

#### `vh setup nginx`
- install or deploy Vaulthalla nginx site config
- enable site where safe
- validate config before reload

#### `vh teardown nginx`
- remove or disable Vaulthalla-managed nginx site config only
- do not assume ownership of the whole nginx installation

#### Avoid for now
- package-manager side effects hidden behind package install
- broad system teardown that removes software the user may still want

### Expected work
This phase likely needs stronger human guidance around your CLI wiring and command system.
Codex can help with planning and scaffolding, but command integration should respect your existing command tree, usage model, async/service patterns, and internal operator structure.

### Exit criteria
- CLI can perform optional integration tasks that package install skips
- Debian package no longer needs to own advanced integration flows
- CLI and package responsibilities are cleanly separated

---

## Phase 6 — Extended CLI integration features

### Objective
Add richer explicit tooling for convenience features that should never live in Debian maintainer scripts.

### Candidate commands/features
- `vh setup nginx --certbot`
- `vh setup nginx --domain <domain>`
- `vh doctor`
- `vh doctor install`
- `vh setup db --local`
- `vh setup db --remote`
- `vh setup status`

### Rationale
These are high-value admin conveniences, but they belong in the Vaulthalla control plane, not in `apt install`.

### Examples
- `vh setup nginx --certbot`
  - may install/configure certbot explicitly because the admin asked for it
- `vh doctor`
  - report DB status, proxy status, service health, initial super-admin state, missing assets, etc.

### Exit criteria
- Optional integrations become discoverable and explicit
- package install stays clean while admin tooling becomes richer

---

## Phase 7 — Documentation and release contract enforcement

### Objective
Make sure future changes do not regress packaging behavior or CLI/install boundaries.

### Scope
- update Debian docs
- update install docs
- update contributor docs
- add or extend release contract tests for:
  - package payload expectations
  - package policy expectations
  - lean install behavior
  - CLI follow-up workflow assumptions

### Required docs
- `debian/README.Debian`
- install docs for:
  - default install
  - lean install with `--no-install-recommends`
  - remote DB setup
  - nginx manual/CLI setup
  - first-use CLI super-admin claim

### Suggested validation matrix
- `apt install vaulthalla`
- `apt install --no-install-recommends vaulthalla`
- install on host with PostgreSQL already present
- install on host with nginx already present
- install on host with conflicting proxy on 80/443
- first `vh` invocation for initial super-admin claim
- CLI-driven `vh setup db`
- CLI-driven `vh setup nginx`

### Exit criteria
- install behavior is documented, enforced, and testable
- package and CLI roles remain stable over time

---

## Codex vs Human split

### Good Codex tasks
- Debian packaging audits and diffs
- maintainer script cleanup
- contract test updates
- docs synchronization
- debconf cleanup/removal
- package metadata adjustments
- identifying drift between `/bin` intent and `/debian` behavior

### Human-led or high-context tasks
- CLI command tree integration
- command wiring in existing usage/operator system
- nuanced UX for admin commands
- destructive/teardown semantics
- certbot orchestration design
- final naming and product-facing UX decisions

---

## Recommended implementation order

1. Finish Debian payload/runtime parity
2. Remove or minimize routine package prompts
3. Shift PostgreSQL/nginx to recommended optional integrations
4. Remove package-time superadmin UID handling
5. Add `vh setup db` and `vh setup nginx`
6. Add richer CLI conveniences like `--certbot`
7. Lock behavior down with docs and contract tests

---

## Target end state

### Common install
```bash
sudo apt install vaulthalla
```

- installs Vaulthalla
- usually brings in recommended postgres/nginx
- performs safe smart setup where obvious
- prints summary of what happened and what was skipped

### Lean / advanced install
```bash
sudo apt install --no-install-recommends vaulthalla
```

- installs Vaulthalla only
- skips optional integrations unless already present and safe
- expects admin to use explicit CLI follow-up where needed

### Follow-up CLI
```bash
vh setup db
vh setup nginx
vh setup nginx --certbot
```

This keeps package install clean while making Vaulthalla’s own tooling the place where richer system orchestration lives.
