# Debian vs `/bin` Lifecycle Audit

Audit date: 2026-04-20  
Scope: semantic drift between Debian maintainer contract (`/debian`) and source/dev installer scripts (`/bin`).

## Summary

- Debian maintainer scripts represent the production package lifecycle contract.
- `/bin` scripts are a source/dev/operator path and are intentionally more manual and destructive.
- One concrete drift bug was fixed in this pass (DB password seed behavior).

## Cross-Contract Findings

1. DB seed mismatch (fixed)
- Debian `postinst`: seeds `/run/vaulthalla/db_password` only when it created a new DB role/password.
- Previous `/bin/setup/install_db.sh`: always wrote a fresh seed even when role already existed.
- Risk: pending seed could diverge from actual role password.
- Resolution: `/bin/setup/install_db.sh` now seeds only when role was created in this run.

2. Service enablement policy differs
- Debian path: explicit `enable --now` for `vaulthalla-web.service`; core/cli handled via `preset` + debhelper lifecycle.
- `/bin/setup/install_systemd.sh`: enables all units directly (`vaulthalla.service`, `vaulthalla-cli.socket`, `vaulthalla-cli.service`, `vaulthalla-web.service`).
- Assessment: intentional operational difference; source installer is not the Debian lifecycle contract.

3. Unit ownership/teardown behavior differs materially
- Debian remove/purge: conservative, package-path scoped cleanup, debhelper-managed systemd transitions.
- `/bin/teardown/uninstall_systemd.sh`: aggressively removes `vaulthalla*` units from `/etc/systemd/system` and `/lib/systemd/system`.
- Assessment: acceptable for source teardown, unsafe to treat as equivalent to package remove/purge.

4. Nginx behavior differs by design
- Debian `postinst`: debconf-gated, conservative nginx auto-config with safety checks and marker-based purge cleanup.
- `/bin` installer path: no equivalent nginx automation.
- Assessment: intentional; Debian package owns nginx integration behavior.

5. Runtime payload assumptions diverge in places
- `/bin/setup/install_dirs.sh` syncs SQL payloads into `/usr/share/vaulthalla/psql`.
- Debian packaging contract does not explicitly stage `deploy/psql` in `debian/rules`/`debian/install`.
- Assessment: potential contract ambiguity; keep tracked as follow-up before changing package payload.

6. Directory mode defaults differ
- Debian path creates `/run/vaulthalla` in maintainer scripts and also ships tmpfiles policy.
- `/bin/setup/install_dirs.sh` sets different runtime dir modes than Debian scripts/tmpfiles policy.
- Assessment: low-risk today but worth keeping explicit to avoid permission drift regressions.

## Operator Safeguards

- Treat `/debian` scripts as authoritative for package lifecycle semantics.
- Use `/bin` scripts for source/dev installs; do not assume remove/purge parity with Debian.
- When changing runtime paths or cleanup behavior, update both:
- `/debian` maintainer scripts and package validation contract
- `/bin` scripts only where source-install behavior should intentionally match

## Deferred Follow-Up Risks

1. Decide whether `/usr/share/vaulthalla/psql` is required in Debian package payload, then codify it in packaging + validator or remove the assumption from source-install docs/scripts.
2. Consider tightening guardrails to prevent running destructive `/bin` teardown scripts on package-managed hosts unintentionally.
