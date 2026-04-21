# Debian Packaging + Lifecycle Contract

High-signal reference for the active Debian package surface under `/debian`.

## Scope

Package: `vaulthalla`  
Primary build/runtime sources: `core/`, `web/`, `deploy/`, `/debian` maintainer scripts.

## File Index (`/debian`)

| File | Responsibility |
| --- | --- |
| `debian/control` | Package metadata, hard runtime deps in `Depends`, PostgreSQL/nginx in `Recommends`. |
| `debian/rules` | Meson build from `core/`; stages non-Meson payloads into `debian/tmp` (config, systemd units, nginx template, docs, web standalone payload, udev/tmpfiles, CLI symlinks). |
| `debian/install` | Final install manifest mapping staged files into package paths. Includes multiarch mappings for libs/udev/tmpfiles. |
| `debian/postinst` | `configure` lifecycle logic: user/group/runtime dirs, optional conservative DB/nginx setup via runtime detection + `VH_SKIP_*` env overrides, systemd preset/enable. |
| `debian/prerm` | `remove|deconfigure` handling: stop/disable core/cli/web units, clear run seeds, disable nginx symlink only when package site path is targeted. |
| `debian/postrm` | `remove` seed cleanup; `purge` state/nginx cleanup with marker checks and conservative boundaries. |
| `debian/tmpfiles.d/vaulthalla.conf` | Runtime/log tmpfiles policy. |
| `debian/vaulthalla.udev` | TPM device group/mode rules. |
| `debian/README.Debian` | Operator-facing install/runtime/remove/purge behavior summary. |
| `debian/README`, `debian/README.source` | Template placeholders; not authoritative contract docs. |
| `debian/copyright` | Debian copyright-format metadata (also shipped in `/usr/share/doc/vaulthalla/copyright`). |

## Installed Payload Contract (Current)

Key package-managed paths:

- Binaries: `/usr/bin/vaulthalla-server`, `/usr/bin/vaulthalla-cli`, `/usr/bin/vaulthalla`, `/usr/bin/vh`
- Config: `/etc/vaulthalla/config.yaml`, `/etc/vaulthalla/config_template.yaml.in`
- Units: `/lib/systemd/system/vaulthalla.service`, `vaulthalla-cli.service`, `vaulthalla-cli.socket`, `vaulthalla-web.service`
- Web runtime: `/usr/share/vaulthalla-web` (Next standalone payload + static assets)
- Nginx template: `/usr/share/vaulthalla/nginx/vaulthalla.conf`
- SQL deploy assets: `/usr/share/vaulthalla/psql`
- TPM/tmpfiles: `/usr/lib/*/udev/rules.d/60-vaulthalla-tpm.rules`, `/usr/lib/*/tmpfiles.d/vaulthalla.conf`
- Docs: `/usr/share/doc/vaulthalla/LICENSE`, `/usr/share/doc/vaulthalla/copyright`

## Prompt Flow

Phase 2 policy removes routine debconf prompting for install setup decisions.
Package install is low-prompt by default; optional integration behavior is
driven by conservative runtime checks and optional environment overrides.

## Lifecycle Semantics

`postinst configure`:

- Ensures `vaulthalla` system user/group and `tss` group membership.
- Creates runtime/state dirs and `/mnt/vaulthalla` when missing.
- Optionally initializes local PostgreSQL role/db when local PostgreSQL is present and healthy.
- Seeds `/run/vaulthalla/db_password` only when a new DB role password was generated.
- Presets core/cli/web units; explicitly enables `vaulthalla-web.service`.
- Applies nginx integration only under safety checks.
- Removes legacy `/run/vaulthalla/superadmin_uid` seed to avoid package-time ownership binding.

`prerm remove|deconfigure`:

- Stops/disables core/cli/web units.
- Removes pending run-seed files.
- Removes nginx enabled symlink only when it points to package site path.

`postrm purge` (superset of remove cleanup):

- Removes package nginx symlink and removes site file only when package-managed marker exists.
- Purges `/var/lib/vaulthalla`, `/var/log/vaulthalla`, attempts empty `/mnt/vaulthalla` removal.
- Deletes `vaulthalla` system user (best-effort).

## Conservative Boundaries / Invariants

- No secrets are written into `/etc/vaulthalla/config.yaml` by maintainer scripts.
- Nginx automation is safe-skip, not force-apply.
- Remove is conservative; purge is the destructive boundary.
- Package-owned nginx cleanup is marker-gated (`/var/lib/vaulthalla/nginx_site_managed`).
- Repeated script invocation is expected to be non-fatal and mostly idempotent.
