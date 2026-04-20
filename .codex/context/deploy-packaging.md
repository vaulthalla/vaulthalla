# Deploy + Debian Map

## Deploy Assets (`deploy/`)

- `deploy/config/config.yaml`: runtime config defaults (websocket/http/db/auth/sync/services/sharing/caching/audit/dev).
- `deploy/config/config_template.yaml.in`: sparse template payload.
- `deploy/psql/*.sql`: ordered SQL schema/migration files (`000_...`, `001_...`, ..., `060_...`).
- `deploy/systemd/*.service.in`: systemd unit templates for core, cli, and web.
- `deploy/systemd/vaulthalla-cli.socket`: Unix socket activation for CLI service.
- `deploy/vaulthalla.env.example`: environment template.

## Systemd Units

Core service (`deploy/systemd/vaulthalla.service.in`):

- Exec: `@BINDIR@/vaulthalla-server`
- User/Group: `vaulthalla`
- Requires PostgreSQL

CLI service (`deploy/systemd/vaulthalla-cli.service.in`):

- Exec: `@BINDIR@/vaulthalla-cli --systemd`
- Restricts to `AF_UNIX`
- Read-write path: `/run/vaulthalla`

Web service (`deploy/systemd/vaulthalla-web.service.in`):

- Exec: `@BINDIR@/node /usr/share/vaulthalla-web/server.js`
- User: `www-data`

## Install Orchestration (`bin/`)

- `bin/install.sh`: dependency/user/dir/core install + DB + systemd.
- `bin/setup/install_dirs.sh`: runtime dirs, config/data payloads, SQL sync, CLI symlinks.
- `bin/setup/install_systemd.sh`: renders `.service.in` templates with `@BINDIR@`, installs socket, enables units.
- `bin/uninstall.sh`: unmount + service teardown + optional DB/user/deps purge.

## Debian Package (`debian/`)

Single package name:

- Source/package: `vaulthalla` (`debian/control`)

Installed payload (`debian/install`) includes:

- Binaries: `vaulthalla-server`, `vaulthalla-cli`, `vaulthalla`, `vh`
- Config: `/etc/vaulthalla/config.yaml`
- Systemd units: core + cli service/socket
- Static libs and docs/manpage

`debian/postinst` behavior:

- creates `vaulthalla` system user/group
- sets up runtime/state/log/mount dirs
- optional PostgreSQL role/db initialization via debconf
- seeds runtime DB password file for TPM sealing flow
- presets systemd units

`debian/changelog` tracks Debian upstream+revision (`X.Y.Z-N`).

## Important Packaging Reality

The repo currently builds/deploys as a single package `vaulthalla`; web-specific Debian files under `web/debian/` are not the active monorepo packaging path by default.
