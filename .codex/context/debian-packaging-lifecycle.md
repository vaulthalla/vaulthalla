# Debian Packaging + Lifecycle Contract

High-signal reference for maintainer-script behavior under `/debian`.

## Scope

- Package: `vaulthalla`
- Lifecycle source of truth: `debian/postinst`, `debian/prerm`, `debian/postrm`
- `/bin` scripts are operator helpers, not Debian lifecycle truth

## Dependency Policy (Current)

- Hard runtime deps in `Depends`: includes `adduser`, `nodejs`, `openssl`.
- Optional integrations in `Recommends`: includes `postgresql`, `nginx`, `swtpm`, `swtpm-tools`.
- Maintainer scripts must tolerate recommended components being absent.

## Installed Payload (Key Paths)

- Binaries: `/usr/bin/vaulthalla-server`, `/usr/bin/vaulthalla-cli`, `/usr/bin/vaulthalla`, `/usr/bin/vh`
- Config: `/etc/vaulthalla/config.yaml`, `/etc/vaulthalla/config_template.yaml.in`
- Units: `vaulthalla.service`, `vaulthalla-cli.service`, `vaulthalla-cli.socket`, `vaulthalla-web.service`, `vaulthalla-swtpm.service`
- Web runtime: `/usr/share/vaulthalla-web`
- SQL deploy assets: `/usr/share/vaulthalla/psql`
- Nginx template payload path: `/usr/share/vaulthalla/nginx/vaulthalla`
- Udev/tmpfiles payload: `/usr/lib/*/udev/rules.d/60-vaulthalla-tpm.rules`, `/usr/lib/*/tmpfiles.d/vaulthalla.conf`

## `postinst configure` Contract

- Handles both fresh install and upgrade (`is_upgrade` derives from non-empty `$2` on `configure`).
- Creates/converges `vaulthalla` user/group + `tss` membership idempotently.
- Converges runtime/state dirs (`/var/lib/vaulthalla`, `/var/log/vaulthalla`, `/run/vaulthalla`, `/etc/vaulthalla`) and creates `/mnt/vaulthalla` only when absent.
- Requires packaged SQL runtime assets at `/usr/share/vaulthalla/psql` and fails if missing/empty (package integrity guard).
- Aligns ownership/mode for package config files without overwriting content.
- Web cache setup is non-destructive:
  - keeps existing real `.next/cache` directory untouched
  - keeps unexpected symlink targets untouched
  - only creates expected symlink when cache path is absent
- Optional DB bootstrap is conservative:
  - skipped when local PostgreSQL is unavailable
  - existing DB/role are preserved during upgrade with no recovery prompt/reseed
  - install-time recovery prompt path only applies to non-upgrade interactive installs
- Optional nginx setup is conservative:
  - requires nginx installed + active + safe layout
  - never overwrites existing `sites-available/vaulthalla`
  - refuses non-symlink `sites-enabled/vaulthalla`
  - reverts newly created enablement link if `nginx -t` fails
- TPM/swtpm behavior:
  - hardware TPM path disables swtpm fallback
  - no-hardware path provisions swtpm fallback when possible
  - swtpm provisioning failure is non-fatal on upgrade, fatal on fresh install
- Most steady-state `systemctl` calls are wrapped best-effort; swtpm start/validation remains a fresh-install failure path when no hardware TPM fallback can be validated.

## `prerm remove|deconfigure` Contract

- Stops/disables package services via safe `systemctl` wrapper.
- Removes transient runtime seed files (`/run/vaulthalla/superadmin_uid`, `/run/vaulthalla/db_password`).
- Removes TPM backend override file.
- Removes nginx enabled symlink only when it points to package site path.

## `postrm` Contract

- `remove`: transient cleanup only (no destructive DB/state teardown).
- `purge` is destructive boundary for package-owned local state.
- Purge nginx site-file removal is marker-gated (`/var/lib/vaulthalla/nginx_site_managed`).
- Purge PostgreSQL cleanup is conservative:
  - preserves by default in noninteractive contexts
  - may prompt in interactive purge
  - falls back to preserve on detection/command failures

## Invariants Future Changes Must Preserve

- Maintainer scripts remain idempotent under repeated `configure` and upgrades.
- Upgrade path must not overwrite `/etc/vaulthalla/config.yaml`.
- Upgrade path must not destructively reset existing DB, nginx, TPM state, or web cache directories.
- Optional integrations (nginx/PostgreSQL/swtpm/systemd) must not hard-fail package upgrades when unavailable.
