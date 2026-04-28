# Debian Packaging + Lifecycle Contract

High-signal reference for package lifecycle behavior under `/debian`.

## Scope

- Package: `vaulthalla`
- Authoritative lifecycle contract: Debian maintainer scripts (`postinst`, `prerm`, `postrm`)
- `/bin` scripts are source/dev/operator helpers, not package lifecycle truth

## Packaging Responsibilities

Debian package is expected to:

- Install binaries, services, config templates, runtime assets, and packaging-managed directories.
- Use `Depends` for hard runtime requirements and `Recommends` for optional integrations.
- Keep `apt install` conservative and low-prompt.
- Apply optional integration setup only when safe and clearly bounded.
- Avoid destructive or ambiguous system mutation in maintainer scripts.

## File Map (`/debian`)

- `control`: metadata and dependency policy
- `rules`: build/staging orchestration from `core/`, `deploy/`, `web/`
- `install`: final path mapping into package payload
- `postinst`: `configure` path (user/group/dirs, optional DB/nginx, systemd actions)
- `prerm`: `remove|deconfigure` service stop/disable and scoped cleanup
- `postrm`: `remove` cleanup and `purge` destructive boundary
- `tmpfiles.d/vaulthalla.conf`: runtime/log dir policy
- `vaulthalla.udev`: TPM udev rules
- `README.Debian`: operator-facing lifecycle notes

## Installed Payload Contract (Key Paths)

- Binaries: `/usr/bin/vaulthalla-server`, `/usr/bin/vaulthalla-cli`, `/usr/bin/vaulthalla`, `/usr/bin/vh`
- Config: `/etc/vaulthalla/config.yaml`, `/etc/vaulthalla/config_template.yaml.in`
- Units: `/lib/systemd/system/vaulthalla.service`, `vaulthalla-cli.service`, `vaulthalla-cli.socket`, `vaulthalla-web.service`
- Web runtime: `/usr/share/vaulthalla-web`
- SQL deploy assets: `/usr/share/vaulthalla/psql`
- Nginx template: `/usr/share/vaulthalla/nginx/vaulthalla.conf`
- Udev/tmpfiles: `/usr/lib/*/udev/rules.d/60-vaulthalla-tpm.rules`, `/usr/lib/*/tmpfiles.d/vaulthalla.conf`

## Lifecycle Semantics

### `postinst configure`

- Ensures `vaulthalla` system user/group and `tss` membership.
- Converges runtime/state directories and `/mnt/vaulthalla` creation when missing.
- Runs conservative optional PostgreSQL bootstrap only when local PostgreSQL is present/usable.
- Preserves existing DB resources by default in noninteractive/conflict paths.
- Applies nginx only under safe checks (installed, active, non-conflicting, package-managed path expectations).
- Removes legacy package-time superadmin seed (`/run/vaulthalla/superadmin_uid`).

### `prerm remove|deconfigure`

- Stops/disables package services.
- Removes pending runtime seed files.
- Removes nginx enabled symlink only when it targets package-managed site path.

### `postrm purge`

- Purge is the destructive boundary for package-owned local state.
- Nginx site-file removal is marker-gated (`/var/lib/vaulthalla/nginx_site_managed`).
- PostgreSQL cleanup is conservative and preserves by default in noninteractive contexts.

## Upgrade and Idempotency Constraints

Maintainer scripts must be safe for repeated `configure` runs and upgrades:

- Treat existing users/groups/dirs/symlinks/services as expected states.
- Avoid fatal failures on “already exists” conditions.
- Keep optional provisioning non-fatal when upgrade should preserve working state.
- Never overwrite user-modified local config unless explicitly package-managed and safe.
- Never perform destructive DB teardown in noninteractive upgrade paths.

## Package vs CLI Boundary

Package lifecycle should provision software and safe defaults; explicit admin workflows belong to CLI.

Typical CLI-owned integration flows:

- local DB setup/teardown
- nginx setup/teardown and advanced options
- explicit remote DB setup and diagnostics

## `/debian` vs `/bin` Drift Guardrails

- `/debian` scripts define package lifecycle semantics; `/bin` may be more manual/destructive.
- Do not assume `/bin/teardown` behavior matches `apt remove/purge`.
- When lifecycle behavior changes, update:
  - maintainer scripts + packaging contract/tests
  - `/bin` helpers only where intentional parity is desired

Known drift categories to watch:

- DB password seeding behavior
- service enable/disable policy differences
- teardown aggressiveness differences
- runtime asset path assumptions (notably SQL payload)
- runtime directory mode drift

## Invariants

- No secret injection into `/etc/vaulthalla/config.yaml` by maintainer scripts.
- Remove remains conservative; purge handles destructive local cleanup.
- Package-managed nginx cleanup remains marker/path scoped.
- Maintainer scripts are expected to be re-runnable without surprise data loss.
