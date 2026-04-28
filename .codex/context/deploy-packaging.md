# Deploy + Packaging Map

## Purpose

Quick map of deploy-time assets and how they feed packaging.  
For maintainer-script lifecycle details, use:

- `.codex/context/debian-packaging-lifecycle.md`

## Deploy Assets (`deploy/`)

- `deploy/config/config.yaml`: default runtime config.
- `deploy/config/config_template.yaml.in`: template config payload.
- `deploy/psql/*.sql`: SQL schema/migration assets used by source/test flows.
- `deploy/systemd/*.service.in`: unit templates for core, CLI, web.
- `deploy/systemd/vaulthalla-cli.socket`: CLI socket unit.
- `deploy/nginx/vaulthalla.conf`: package nginx site template source.

## Systemd Template Contract

- Core service: `@BINDIR@/vaulthalla-server`, user/group `vaulthalla`.
- CLI service: `@BINDIR@/vaulthalla-cli --systemd`, unix-socket restricted.
- Web service: `@BINDIR@/node /usr/share/vaulthalla-web/server.js`, user/group `vaulthalla`.

## Packaging Path (Canonical)

- CI packaging entrypoint: `.github/actions/package/action.yml`
- Build tooling: `python3 -m tools.release build-deb`
- Contract validation: `python3 -m tools.release validate-release-artifacts`

`debian/rules` builds from `core/` and stages non-Meson payloads from `deploy/` and `web/` into the package.

## `/bin` Script Boundary

`/bin` scripts are source/dev/operator install helpers, not Debian maintainer-script replacements.  
Do not assume `/bin` install/uninstall semantics match `apt remove/purge` behavior.
