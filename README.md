# Vaulthalla | The Final Cloud

[![build](https://img.shields.io/github/actions/workflow/status/vaulthalla/vaulthalla/build_and_test.yml?label=build)](https://github.com/vaulthalla/vaulthalla/actions)
[![release](https://img.shields.io/github/v/release/vaulthalla/vaulthalla?display_name=tag&sort=semver)](https://github.com/vaulthalla/vaulthalla/releases)
[![license](https://img.shields.io/github/license/vaulthalla/vaulthalla?v=2)](https://github.com/vaulthalla/vaulthalla/blob/main/LICENSE)
[![debian-first](https://img.shields.io/badge/debian-first-8A2BE2)](https://github.com/vaulthalla/vaulthalla)
[![linux-native](https://img.shields.io/badge/linux-native-2ea44f)](https://github.com/vaulthalla/vaulthalla)
[![AES-256-GCM/NI](https://img.shields.io/badge/AES--256--GCM%2FNI-encrypted-cyan)](https://github.com/vaulthalla/vaulthalla)

Vaulthalla is a Linux-native self-hosted cloud platform with a compiled core daemon,
native FUSE integration, encrypted storage workflows, and S3-compatible sync.

## Read This First

- Vaulthalla is system-level software: install and operational setup require root privileges.
- The recommended convenience path is the hosted installer script.
- Package install is intentionally low-prompt and conservative; optional DB/nginx steps run only when local checks pass.
- Package install does not pre-bind initial super-admin ownership. First-use CLI flow handles that.
- CLI commands are the explicit control plane for optional integration setup.
- Developer mode (`-d`) is destructive and intended only for disposable environments.

## Why Vaulthalla

| Capability | Current Model |
| --- | --- |
| Compiled runtime | C++ core with systemd-managed services on Linux |
| Filesystem integration | Native FUSE runtime with default mount path at `/mnt/vaulthalla` |
| Security posture | libsodium-based encryption, TPM-aware host integration, RBAC-driven command surface |
| Storage backends | Local runtime + S3-compatible providers |
| Deployment model | Debian-first packaging, CLI-driven integration setup |

## Installation

### Recommended Convenience Installer

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash
```

This is the recommended path for most operators. It performs one no-prompt flow:
repo bootstrap, apt install, and post-install CLI onboarding.

Advanced interactive flow (arrow-key profile + onboarding selections):

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash -s -- --interactive
```

If running from a local clone instead of the hosted endpoint:

```bash
./bin/vh/install.sh
./bin/vh/install.sh --interactive
```

### Debian / Ubuntu Package (Manual / Transparent Alternative)

```bash
sudo curl -fsSL https://apt.vaulthalla.sh/pubkey.gpg \
  -o /etc/apt/trusted.gpg.d/vaulthalla.gpg

echo "deb [arch=amd64] https://apt.vaulthalla.sh stable main" | \
  sudo tee /etc/apt/sources.list.d/vaulthalla.list > /dev/null

sudo apt update
sudo apt install vaulthalla
```

Install modes:

```bash
sudo apt install vaulthalla
sudo apt install --no-install-recommends vaulthalla
VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla
VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla
```

`apt install` provisions package payloads, services, and conservative optional integration behavior.
It does not replace explicit operator setup flows.
Package install may stage a baseline nginx template at
`/usr/share/vaulthalla/nginx/vaulthalla.conf`; final managed deployment is via `vh setup nginx`.

### Post-Install CLI Setup

```bash
vh setup assign-admin
vh setup db
vh setup remote-db
vh setup nginx
vh setup nginx --certbot --domain <domain>
vh teardown nginx
```

- `vh setup assign-admin` is the explicit admin-claim verification/claim command.
- `vh setup db` is for local PostgreSQL bootstrap.
- `vh setup remote-db` is for explicit remote PostgreSQL configuration.
- `vh setup nginx` is the canonical final deployment path for Vaulthalla-managed nginx config.
- `vh setup nginx --certbot --domain <domain>` is the explicit certbot-backed flow.
- `vh teardown nginx` removes only Vaulthalla-managed nginx integration.

Local DB bootstrap and remote DB setup are intentionally separate workflows.
Schema/migration ownership remains with runtime startup (`SqlDeployer`).

### Verify Services

```bash
sudo systemctl status vaulthalla.service
sudo systemctl status vaulthalla-cli.service
sudo systemctl status vaulthalla-cli.socket
sudo systemctl status vaulthalla-web.service
sudo journalctl -fu vaulthalla.service
```

## Runtime Defaults

- Main config: `/etc/vaulthalla/config.yaml`
- Runtime directory: `/run/vaulthalla`
- State directory: `/var/lib/vaulthalla`
- Log directory: `/var/log/vaulthalla`
- SQL deploy assets: `/usr/share/vaulthalla/psql`
- Packaged nginx template: `/usr/share/vaulthalla/nginx/vaulthalla.conf`

## Build from Source (Development Preview)

For development/testing only:

```bash
git clone https://github.com/vaulthalla/vaulthalla.git
cd vaulthalla
sudo make install -- -d
```

`-d` enables volatile developer mode and can reset local Vaulthalla state.
Do not use it on hosts with production data.

## Documentation

- Debian operator/install policy (repo): [`debian/README.Debian`](debian/README.Debian)
- Debian operator/install policy (installed package): `/usr/share/doc/vaulthalla/README.Debian`
- Packaging/distribution notes: [`DISTRIBUTION.md`](DISTRIBUTION.md)
- Web app notes: [`web/README.md`](web/README.md)

## Support and Contribution

Issues and PRs are welcome.
