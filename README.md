![vaulthalla-v1-release-banner](https://docs-media.valkyrianlabs.com/Vaulthalla-v1.0.0-release-banner-a-1200x630.png)

[![build](https://img.shields.io/github/actions/workflow/status/vaulthalla/vaulthalla/build_and_test.yml?label=build)](https://github.com/vaulthalla/vaulthalla/actions)
[![release](https://img.shields.io/github/v/release/vaulthalla/vaulthalla?display_name=tag\&sort=semver)](https://github.com/vaulthalla/vaulthalla/releases)
[![license](https://img.shields.io/github/license/vaulthalla/vaulthalla?v=2)](https://github.com/vaulthalla/vaulthalla/blob/main/LICENSE)
[![debian-first](https://img.shields.io/badge/debian-first-8A2BE2)](https://github.com/vaulthalla/vaulthalla)
[![linux-native](https://img.shields.io/badge/linux-native-2ea44f)](https://github.com/vaulthalla/vaulthalla)
[![AES-256-GCM/NI](https://img.shields.io/badge/AES--256--GCM%2FNI-encrypted-cyan)](https://github.com/vaulthalla/vaulthalla)

Vaulthalla is a Linux-native self-hosted cloud platform built around a compiled core daemon, privileged FUSE integration, encrypted storage workflows, TPM-aware secret handling, RBAC-driven access control, a CLI control plane, and a packaged web console.

It is designed for operators who want cloud storage to behave like infrastructure again: mounted, controlled, encrypted, inspectable, and owned by the machine it runs on.

## ⚠️ Read This First

Vaulthalla is system-level Linux software.

It installs services, prepares runtime directories, mounts a native FUSE filesystem, manages local control sockets, uses PostgreSQL for runtime state, and can integrate with TPM/swtpm, Nginx, and Certbot.

That is intentional. Vaulthalla is designed to run as host infrastructure, not as a self-contained web app.

### What installation may prepare

Installing Vaulthalla with `sudo apt install vaulthalla` or the hosted installer may:

* create the `vaulthalla` system user and group
* install systemd units
* prepare `/etc/vaulthalla`, `/var/lib/vaulthalla`, `/run/vaulthalla`, and `/var/log/vaulthalla`
* prepare the default FUSE mount path at `/mnt/vaulthalla`
* configure runtime permissions for the daemon and CLI socket
* prepare or reuse local PostgreSQL resources when the local DB flow is enabled
* use hardware TPM2 when available
* provision managed `swtpm` fallback when hardware TPM is unavailable
* stage packaged web console assets under `/usr/share/vaulthalla-web`
* prepare writable web runtime cache under `/var/cache/vaulthalla-web`

Optional host integrations are handled through explicit setup commands:

* `vh setup nginx` installs the managed Vaulthalla Nginx site
* `vh setup nginx --certbot` allows Certbot to modify that managed site

Package installation is conservative by default. Environment-specific setup is kept behind explicit CLI flows where possible.

### Operational expectations

Vaulthalla is probably not the right fit if you want a self-contained web app that never touches the host beyond one process and one config file.

It is built for operators who want Linux-native self-hosted storage with a real daemon, a real filesystem surface, encrypted workflows, RBAC, TPM-aware secret handling, and explicit host-level control.

Welcome to the kernel, brother.

## Quick Start

### Recommended Installer

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash
```

Interactive installer flow:

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash -s -- --interactive
```

Local clone installer:

```bash
./bin/vh/install.sh
./bin/vh/install.sh --interactive
```

The hosted installer performs the standard operator path:

* configures the Vaulthalla APT repository
* installs the Debian package
* prepares runtime directories and services
* performs conservative package-time setup
* hands off explicit operator setup to the `vh` CLI

### Manual Debian / Ubuntu Install

```bash
sudo curl -fsSL https://apt.vaulthalla.sh/pubkey.gpg \
  -o /etc/apt/trusted.gpg.d/vaulthalla.gpg

echo "deb [arch=amd64] https://apt.vaulthalla.sh stable main" | \
  sudo tee /etc/apt/sources.list.d/vaulthalla.list > /dev/null

sudo apt update
sudo apt install vaulthalla
```

Optional install modes:

```bash
sudo apt install vaulthalla
sudo apt install --no-install-recommends vaulthalla
VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla
VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla
```

### Post-Install Setup

Claim/verify admin ownership:

```bash
vh setup assign-admin
```

Local PostgreSQL bootstrap:

```bash
vh setup db
```

Remote PostgreSQL configuration:

```bash
vh setup remote-db
```

Nginx integration:

```bash
sudo vh setup nginx --domain <domain>
```

Nginx + Certbot integration:

```bash
sudo vh setup nginx --domain <domain> --certbot
```

If a managed Vaulthalla Nginx site already has a domain, Certbot can also be run later against that managed site:

```bash
sudo vh setup nginx --certbot
```

Remove only Vaulthalla-managed Nginx integration:

```bash
sudo vh teardown nginx
```

## Verify Runtime

```bash
vh status
```

Service checks:

```bash
sudo systemctl status vaulthalla.service
sudo systemctl status vaulthalla-cli.service
sudo systemctl status vaulthalla-cli.socket
sudo systemctl status vaulthalla-web.service
sudo systemctl status vaulthalla-swtpm.service
```

Live logs:

```bash
sudo journalctl -fu vaulthalla.service
sudo journalctl -fu vaulthalla-web.service
```

## What Vaulthalla Ships Today

| Capability             | Current Model                                                        |
| ---------------------- | -------------------------------------------------------------------- |
| Core runtime           | Compiled C++ daemon managed by systemd                               |
| Filesystem integration | Native FUSE mount, default path `/mnt/vaulthalla`                    |
| CLI control plane      | `vh` command surface backed by local runtime IPC                     |
| Web console            | Packaged Next.js standalone runtime behind `vaulthalla-web.service`  |
| Database               | PostgreSQL-backed metadata and runtime state                         |
| Secret handling        | Hardware TPM2 when available, managed `swtpm` fallback when not      |
| Encryption             | AES-256-GCM/NI-oriented encrypted storage workflows                  |
| Access control         | RBAC-driven admin and vault role model                               |
| Sharing model          | Authenticated user/vault access; public link sharing planned post-v1 |
| Deployment             | Debian-first packaging with explicit Nginx/Certbot setup             |
| Storage providers      | Local runtime plus S3-compatible provider workflows                  |

## Why These Pieces Exist

### Native FUSE

Vaulthalla is not just a web UI for files. The filesystem is part of the product.

FUSE gives Vaulthalla a Linux-native surface area: files can be mounted, traversed, and operated on like real filesystem entries while the daemon enforces Vaulthalla’s storage, metadata, and permission model underneath.

### CLI First

The CLI is the control plane.

Privileged setup, admin ownership, DB configuration, Nginx integration, teardown, status checks, and operational flows are explicit commands. This keeps dangerous setup actions visible instead of hiding them behind web buttons that may or may not have enough context to safely mutate the host.

### TPM-Aware Secrets

Vaulthalla treats host secrets as infrastructure, not as loose strings in a random config file.

On machines with TPM2 hardware, Vaulthalla uses the host TPM path. On VPS and virtualized environments without exposed TPM hardware, the package can provision a managed `swtpm` backend. The goal is to avoid silent plaintext downgrades while keeping the product deployable on real servers operators actually rent.

### RBAC Before Public Links

Vaulthalla v1 focuses on authenticated, role-scoped collaboration.

You can create users, assign scoped privileges, and share access through Vaulthalla’s user/vault model. Public unauthenticated links and external upload/drop links are planned post-v1 because they require careful handling of expiry, revocation, abuse boundaries, rate limiting, and auditability.

### Debian First

Vaulthalla is packaged as system software because it behaves like system software.

The Debian package owns installed artifacts, systemd units, runtime directories, service wiring, and conservative post-install setup. Mutable runtime state lives under the appropriate `/var`, `/run`, and `/etc` paths. Packaged artifacts stay under `/usr/share`.

## Install-Time TPM Behavior

During package configuration:

* Hardware TPM hosts (`/dev/tpmrm0` or `/dev/tpm0`) use hardware TPM.
* Hosts without hardware TPM use managed `swtpm` fallback through `vaulthalla-swtpm.service`.
* Managed `swtpm` state lives under `/var/lib/swtpm/vaulthalla`.
* AppArmor-aware setup is handled for the managed software TPM path.
* If no viable TPM backend is available, setup fails with actionable logs.

Lean installs using `--no-install-recommends` on TPM-less hosts should install `swtpm` and `swtpm-tools` before retrying configure.

## Runtime Defaults

* Main config: `/etc/vaulthalla/config.yaml`
* Runtime directory: `/run/vaulthalla`
* State directory: `/var/lib/vaulthalla`
* Log directory: `/var/log/vaulthalla`
* Default FUSE mount: `/mnt/vaulthalla`
* Software TPM state: `/var/lib/swtpm/vaulthalla`
* SQL deploy assets: `/usr/share/vaulthalla/psql`
* Web runtime payload: `/usr/share/vaulthalla-web`
* Web runtime cache: `/var/cache/vaulthalla-web`
* Packaged Nginx template: `/usr/share/vaulthalla/nginx/vaulthalla.conf`

## Removal, Purge, and Reinstall

Normal remove preserves local data:

```bash
sudo apt remove vaulthalla
```

Purge removes package configuration and may offer optional destructive local PostgreSQL cleanup when interactive:

```bash
sudo apt purge vaulthalla
```

Default behavior is conservative:

* `apt remove` preserves local PostgreSQL role/database data.
* Interactive purge can offer local DB/role teardown.
* Noninteractive purge preserves local PostgreSQL resources.
* Manual local DB teardown is explicit:

```bash
sudo vh teardown db
```

Reinstalling with preserved local PostgreSQL resources requires either:

* interactive reuse flow with password handoff
* destructive drop/recreate
* manual reseed of `/run/vaulthalla/db_password`

Manual reseed example:

```bash
sudo install -d -m 0755 /run/vaulthalla
sudo install -m 0600 -o vaulthalla -g vaulthalla /path/to/db_password /run/vaulthalla/db_password
sudo systemctl restart vaulthalla
```

## Build from Source

Development preview only:

```bash
git clone https://github.com/vaulthalla/vaulthalla.git
cd vaulthalla
sudo make install -- -d
```

`-d` enables volatile developer mode and can reset local Vaulthalla state.

Do not use developer mode on hosts with production data.

## Documentation

* Debian operator/install policy: [`debian/README.Debian`](debian/README.Debian)
* Installed Debian policy doc: `/usr/share/doc/vaulthalla/README.Debian`
* Packaging/distribution notes: [`DISTRIBUTION.md`](DISTRIBUTION.md)
* Web app notes: [`web/README.md`](web/README.md)

## Current v1 Notes

Vaulthalla v1 is a foundation release. The core daemon, CLI, packaging, TPM/swtpm integration, FUSE runtime, authenticated access model, Nginx/Certbot flow, and packaged web console are the priority.

Known post-v1 work includes:

* public unauthenticated link sharing
* public upload/drop links
* deeper web console polish
* broader install validation across more VPS providers
* expanded contributor-facing docs and issue templates

## Support and Contribution

Issues and pull requests are welcome.

Vaulthalla touches privileged Linux services, filesystem integration, PostgreSQL, TPM/swtpm, AppArmor, Nginx, Certbot, and a packaged web runtime. Good contributions are scoped, reproducible, and clear about which subsystem they affect.

If you are reporting an install issue, include:

```bash
vh status
sudo systemctl status vaulthalla.service --no-pager
sudo systemctl status vaulthalla-web.service --no-pager
sudo journalctl -u vaulthalla.service -n 150 --no-pager
sudo journalctl -u vaulthalla-web.service -n 150 --no-pager
```

Welcome to the kernel, brother.
