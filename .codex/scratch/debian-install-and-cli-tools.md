# Debian Install and CLI Tools Staging Plan

## Goal

Refactor Vaulthalla’s Debian install behavior so that:

* `apt install vaulthalla` remains clean, conservative, and low-prompt
* common-case installs still “just work” where safe
* optional integrations like PostgreSQL bootstrap, nginx configuration, and certbot orchestration move toward explicit CLI-driven flows
* packaging and CLI responsibilities are clearly separated
* release contract tests enforce the intended install behavior going forward

This plan intentionally splits Debian package concerns from richer system orchestration concerns.

---

## Design Direction

### Debian package responsibilities

The Debian package should:

* install binaries, configs, service files, packaged assets, and required runtime directories
* install recommended helper packages through standard package metadata only
* perform only conservative, deterministic, safe post-install actions
* avoid broad system mutation when intent is ambiguous
* avoid heavy interactive prompting in the common path
* never rely on dev install scripts or shell-session voodoo for behavior

### CLI responsibilities

The Vaulthalla CLI should become the explicit control plane for optional/admin actions such as:

* local DB bootstrap
* nginx deployment/configuration
* certbot orchestration
* optional teardown/reset flows
* future diagnostics / “doctor” style checks

This creates a clean boundary:

* package manager provisions software
* Vaulthalla CLI provisions Vaulthalla integrations

---

## Phase 1 — Debian install contract stabilization

### Objective

Finish stabilizing the Debian package so install/remove/purge behavior is correct and runtime parity is trustworthy.

### Scope

* Ensure all packaged runtime assets are installed from canonical sources
* Ensure packaged paths match runtime expectations
* Ensure maintainer scripts align with safe setup/teardown semantics
* Remove drift between `/bin` production intent and `/debian` packaging behavior

### Expected work

* Audit and repair:

  * `debian/install`
  * `debian/rules`
  * `debian/postinst`
  * `debian/prerm`
  * `debian/postrm`
  * `debian/control`
* Verify packaged install contains:

  * SQL migration/deploy assets
  * required `/usr/share/vaulthalla/*` payloads
  * expected `/etc/vaulthalla/*` config files
  * required `/var/lib/vaulthalla` and `/run/vaulthalla` directories
* Ensure remove vs purge semantics remain conservative and correct
* Ensure contract tests fail CI when runtime assets are missing

### Exit criteria

* Fresh package install no longer fails due to missing runtime assets
* Package payload matches runtime expectations
* Release contract tests cover core package payload/install contract

---

## Phase 2 — Debian install policy and bootstrap cleanup

### Objective

Collapse prompt cleanup, package policy cleanup, and initial ownership cleanup into one coherent install-surface pass.

### Policy direction

Routine interactive prompts during `apt install` are undesirable unless there is a meaningful conflict that actually requires user intent.

The package should:

* avoid routine debconf prompting
* treat PostgreSQL and nginx as recommended optional integrations
* support lean installs via `--no-install-recommends`
* allow explicit advanced override via install-time environment flags where needed
* remove package-time superadmin UID binding entirely
* defer first CLI super-admin ownership to first-use flow

### Scope

* Audit current `debian/templates`, `debian/config`, and debconf wiring
* Remove dead or misleading template scaffolding unless conflict-only prompting is still justified
* Reclassify PostgreSQL/nginx package relationships
* Update `postinst` to use smart, conservative behavior based on:

  * explicit override env vars
  * actual installed package state
  * safe/conflict-aware runtime checks
* Remove package-time `superadmin-uid` prompting and ownership binding

### Intended install behavior

#### Normal install

```bash
sudo apt install vaulthalla
```

Expected behavior:

* installs Vaulthalla
* usually pulls in recommended PostgreSQL and nginx
* bootstraps local DB if package/state checks say it is safe
* configures nginx if package/state checks say it is safe
* skips risky or conflicting actions and reports what happened
* does not prompt in the common path

#### Lean / advanced install

```bash
sudo apt install --no-install-recommends vaulthalla
```

Expected behavior:

* installs Vaulthalla only
* does not pull in recommended PostgreSQL/nginx
* skips corresponding setup unless they are already present and safe
* reports clear next steps for manual follow-up

#### Explicit advanced override path

Allow targeted opt-out via explicit install-time environment variables, for example:

* `VH_SKIP_DB_BOOTSTRAP=1`
* `VH_SKIP_NGINX_CONFIG=1`

This supports advanced installs without forcing prompts or a full setup wizard.

### Super-admin bootstrap final behavior

* package install does not prompt for Linux UID
* package install does not attempt initial super-admin ownership binding
* first Linux user to invoke `vh` or `vaulthalla` claims initial CLI super-admin ownership
* install completion messaging explains this clearly
* future users still require explicit Linux UID mapping in Vaulthalla where appropriate

### Expected work

* Audit and repair:

  * `debian/templates`
  * `debian/config`
  * `debian/postinst`
  * `debian/control`
  * related docs/tests
* Remove unnecessary routine prompt behavior
* Move `postgresql` from `Depends` to `Recommends`
* Evaluate nginx package classification in the same spirit
* Ensure maintainer scripts never install packages themselves
* Ensure noninteractive install never hangs or behaves unpredictably
* Add clear install summary messaging for:

  * DB bootstrap taken/skipped
  * nginx config taken/skipped
  * first CLI super-admin claim next step

### Exit criteria

* No unnecessary routine package prompts
* No package-time UID prompt
* Package metadata matches the intended install philosophy
* `apt install vaulthalla` works as a clean smart default
* `apt install --no-install-recommends vaulthalla` works as a clean lean path
* Explicit override env vars are supported for advanced installs
* Docs/tests clearly reflect the new policy

---

## Phase 3 — Minimal CLI integration commands

### Objective

Introduce explicit CLI commands for optional integrations that Debian should not fully own.

### Initial target commands

* `vh setup db`
* `vh setup nginx`
* `vh teardown nginx`
* optionally later:

  * `vh teardown db`
  * `vh doctor`
  * `vh setup status`

### Important boundary

These commands should manage Vaulthalla-specific configuration/bootstrap tasks, not recklessly uninstall arbitrary system packages unless that behavior is very explicit and intentional.

### Recommended semantics

#### `vh setup db`

* bootstrap Vaulthalla local PostgreSQL role/database against a local PostgreSQL installation
* rely on the canonical packaged/runtime schema + migration flow already owned by `SqlDeployer`
* do **not** evolve into a generic arbitrary SQL installer
* do **not** make custom user-provided `.sql` ingestion a first-class long-term feature unless explicitly revisited later
* after successful local DB bootstrap, hand off to normal runtime startup behavior so Vaulthalla can consume the seeded DB password and perform any needed startup-driven schema install / migration work

#### `vh setup nginx`

* install or deploy Vaulthalla nginx site config
* enable site where safe
* validate config before reload

#### `vh teardown nginx`

* remove or disable Vaulthalla-managed nginx site config only
* do not assume ownership of the whole nginx installation

#### Avoid for now

* package-manager side effects hidden behind package install
* broad system teardown that removes software the user may still want
* arbitrary user-provided `.sql` schema injection as a promoted feature
* remote DB configuration workflows in this phase

### Expected work

This phase likely needs stronger human guidance around your CLI wiring and command system.
Codex can help with planning and scaffolding, but command integration should respect your existing command tree, usage model, async/service patterns, and internal operator structure.

### Exit criteria

* CLI can perform optional integration tasks that package install skips
* Debian package no longer needs to own advanced integration flows
* CLI and package responsibilities are cleanly separated

---

## Phase 4 — Extended CLI integration features

### Objective

Add richer explicit tooling for convenience features that should never live in Debian maintainer scripts.

### Candidate commands/features

* `vh setup nginx --certbot`
* `vh setup nginx --domain <domain>`
* `vh doctor`
* `vh doctor install`
* `vh setup remote-db`
* or equivalent explicit remote DB configuration command naming to be finalized later
* `vh setup status`

### Rationale

These are high-value admin conveniences, but they belong in the Vaulthalla control plane, not in `apt install`.

Remote DB should be treated as an explicit advanced configuration workflow, not as hidden behavior inside basic local DB bootstrap. This flow should update the canonical Vaulthalla config file through existing config helpers and seed the provided DB password through the normal runtime-consumed mechanism.

### Examples

* `vh setup nginx --certbot`

  * may install/configure certbot explicitly because the admin asked for it
* `vh doctor`

  * report DB status, proxy status, service health, initial super-admin state, missing assets, etc.
* `vh setup remote-db`

  * prompt for remote DB host/port/user/database settings
  * update `config.yaml` through existing config helpers
  * seed remote DB password using the same normal runtime-consumed path
  * restart/reload the service as appropriate so the new config takes effect cleanly

### Exit criteria

* Optional integrations become discoverable and explicit
* package install stays clean while admin tooling becomes richer

---

## Phase 5 — Documentation and release contract enforcement

### Objective

Make sure future changes do not regress packaging behavior or CLI/install boundaries.

### Scope

* update Debian docs
* update install docs
* update contributor docs
* add or extend release contract tests for:

  * package payload expectations
  * package policy expectations
  * lean install behavior
  * CLI follow-up workflow assumptions

### Required docs

* `debian/README.Debian`
* install docs for:

  * default install
  * lean install with `--no-install-recommends`
  * advanced install with explicit env-var overrides
  * remote DB setup
  * nginx manual/CLI setup
  * first-use CLI super-admin claim

### Suggested validation matrix

* `apt install vaulthalla`
* `apt install --no-install-recommends vaulthalla`
* `VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla`
* `VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla`
* install on host with PostgreSQL already present
* install on host with nginx already present
* install on host with conflicting proxy on 80/443
* first `vh` invocation for initial super-admin claim
* CLI-driven `vh setup db`
* CLI-driven `vh setup nginx`

### Exit criteria

* install behavior is documented, enforced, and testable
* package and CLI roles remain stable over time

---

## Codex vs Human split

### Good Codex tasks

* Debian packaging audits and diffs
* maintainer script cleanup
* contract test updates
* docs synchronization
* debconf cleanup/removal
* package metadata adjustments
* identifying drift between `/bin` intent and `/debian` behavior

### Human-led or high-context tasks

* CLI command tree integration
* command wiring in existing usage/operator system
* nuanced UX for admin commands
* destructive/teardown semantics
* certbot orchestration design
* final naming and product-facing UX decisions

---

## Recommended implementation order

1. Finish Debian payload/runtime parity
2. Collapse prompt/package/bootstrap cleanup into one Debian install policy pass
3. Add `vh setup db` and `vh setup nginx`
4. Add richer CLI conveniences like `--certbot`
5. Lock behavior down with docs and contract tests

---

## Target end state

### Common install

```bash
sudo apt install vaulthalla
```

* installs Vaulthalla
* usually brings in recommended postgres/nginx
* performs safe smart setup where obvious
* prints summary of what happened and what was skipped

### Lean / advanced install

```bash
sudo apt install --no-install-recommends vaulthalla
```

* installs Vaulthalla only
* skips optional integrations unless already present and safe
* expects admin to use explicit CLI follow-up where needed

### Targeted advanced override install

```bash
VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla
VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla
```

* keeps the normal install path clean
* allows power users to opt out of specific automatic setup behavior
* avoids forcing routine interactive prompts

### Follow-up CLI

```bash
vh setup db
vh setup nginx
vh setup nginx --certbot
# later:
# vh setup remote-db
```

`vh setup db` is intended to handle local DB bootstrap only. Remote DB configuration belongs in a later explicit advanced CLI workflow rather than being folded into the base local DB bootstrap command.
