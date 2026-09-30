# Deploy assets + Debian package lifecycle

Single binary package: **`vaulthalla`**. Maintainer scripts are the lifecycle source of truth:
`debian/postinst`, `debian/prerm`, `debian/postrm`. The `bin/` scripts are source/dev helpers, and their
semantics do **not** match `apt remove/purge`.

## Deploy assets (`deploy/`)

- `config/config.yaml` (default runtime config, ports 36969/36970/39000), `config/config_template.yaml.in`.
  A gitignored repo-root `config.yaml` overrides it for source installs (`bin/setup/install_dirs.sh`).
- `psql/000…097_*.sql`: schema + ordered migrations → `/usr/share/vaulthalla/psql`.
- `systemd/`: `vaulthalla.service.in` (server, user `vaulthalla`), `vaulthalla-cli.service.in` +
  `vaulthalla-cli.socket` (`--systemd`, unix socket), `vaulthalla-web.service.in` (`node
  /usr/share/vaulthalla-web/server.js`), `vaulthalla-swtpm.service.in` (software TPM fallback).
- `nginx/vaulthalla.conf` → `/usr/share/vaulthalla/nginx/vaulthalla` (template, not auto-enabled).
- `lifecycle/` (Python `main.py` + tests) → `/usr/lib/vaulthalla/lifecycle`: backs `vh setup/teardown`
  host operations.
- `vaulthalla.env`, `bashrc` are **gitignored local secret files**. Never print or commit them.

## Installed payload (key paths)

Binaries `/usr/bin/{vaulthalla-server,vaulthalla-cli,vaulthalla,vh}` · config `/etc/vaulthalla/` ·
runtime `/run/vaulthalla` (CLI socket, transient `db_password` seed) · state `/var/lib/vaulthalla` ·
logs `/var/log/vaulthalla` · mount `/mnt/vaulthalla` · web `/usr/share/vaulthalla-web` · SQL
`/usr/share/vaulthalla/psql` · udev `/usr/lib/udev/rules.d/60-vaulthalla-tpm.rules` · tmpfiles
`/usr/lib/tmpfiles.d/vaulthalla.conf` · needrestart policy conf · letsencrypt renewal deploy hook ·
`vh.1` manpage.

`debian/control`: Depends include `adduser nodejs openssl`. Recommends: `postgresql nginx swtpm swtpm-tools certbot
python3-certbot-nginx python3-certbot-dns-cloudflare`. Maintainer scripts must tolerate every
Recommends being absent.

## `postinst configure`

- Handles fresh install and upgrade. `is_upgrade` is true when `$2` is non-empty.
- Converges the `vaulthalla` user/group and `tss` membership, plus the runtime/state dirs, idempotently. It creates `/mnt/vaulthalla`
  only if absent. **It calls `mountpoint -q` on it with no timeout (`is_mountpoint`, ~L67), so this
  hangs forever if the running FUSE daemon is wedged.** See P0-2 in `production-hardening.md`.
- It fails if `/usr/share/vaulthalla/psql` is missing or empty (package integrity guard).
- Config ownership/mode is aligned without overwriting content.
- Web `.next/cache`: it keeps a real dir or an unexpected symlink, and only creates the expected symlink when the path is absent.
- DB bootstrap is conservative. It's skipped without local PostgreSQL, the DB/role are preserved on upgrade, and the recovery
  prompt only appears on non-upgrade interactive installs. `VH_SKIP_DB_BOOTSTRAP=1` skips it.
- nginx needs to be installed, active, and in a safe layout. It never overwrites `sites-available/vaulthalla`, refuses a
  non-symlink `sites-enabled/vaulthalla`, and reverts its link if `nginx -t` fails. `VH_SKIP_NGINX_CONFIG=1` skips it.
- TPM: a hardware TPM disables swtpm. Without one, swtpm is provisioned. Provisioning failure is fatal on fresh
  install and non-fatal on upgrade.

## `prerm remove|deconfigure`

It stops and disables units through a safe `systemctl` wrapper, removes the transient `/run/vaulthalla/{superadmin_uid (legacy),db_password}`,
removes the TPM backend override, and removes the nginx enabled link only if it points at the package site.

## `postrm`

`remove` does transient cleanup only. **`purge` is the destructive boundary.** nginx site removal is gated on the marker
`/var/lib/vaulthalla/nginx_site_managed`. PostgreSQL is preserved by default when noninteractive, may prompt when
interactive, and falls back to preserve on any detection failure.

## Invariants

- Maintainer scripts stay idempotent across repeated `configure` and upgrades.
- Upgrades never overwrite `/etc/vaulthalla/config.yaml` and never destructively reset the DB, nginx, TPM state, or web cache.
- An unavailable optional integration (nginx/PostgreSQL/swtpm/systemd) never hard-fails an upgrade.
- **Maintainer scripts must never block indefinitely.** Anything that touches the FUSE mount or a live
  service needs a timeout. This is new and not yet enforced; see `production-hardening.md`.

## Teardown safety (`bin/teardown/*`)

- **Never kill processes by substring** (`pkill -f`, `pgrep -f vaulthalla`, `killall`, `fuser -k`). JetBrains
  Remote and agent processes on dev VMs carry `/srv/vaulthalla` in their argv. The only permitted kill is the
  verified `vaulthalla.service` MainPID whose `/proc/$pid/exe` is the server binary, behind `--force-vaulthalla-pids`.
- A busy mount gets a diagnostics report (`lsof`, `fuser -vm`), and holders are never killed. Teardown still falls back
  to lazy unmount (`fusermount3 -uz`, `umount -l`).
- Unit and dir removal targets exact Vaulthalla paths only, and refuses to remove mounted paths.
