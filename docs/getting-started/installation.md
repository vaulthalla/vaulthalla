---
title: Installation
description: Install Vaulthalla from the APT repository, choose an install profile, and verify the package lifecycle.
order: 10
status: published
tags:
  - install
  - setup
  - operator
---

# Installation

Vaulthalla is packaged for Linux systems that use APT, systemd, FUSE, PostgreSQL, Nginx, and TPM-compatible secret protection. The recommended path is the signed APT repository. Source installs are useful for development only.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Before You Install

Plan these items first:

- A Linux host with systemd.
- A user account that will operate `vh`.
- PostgreSQL, either local or remote.
- Nginx if you want the web console exposed through a domain.
- Hardware TPM through `/dev/tpmrm0` or `/dev/tpm0`, or the packaged `swtpm` fallback.
- Enough disk space for `/var/lib/vaulthalla`, PostgreSQL, cache data, and any local vault bodies.

:::callout{theme="warning" title="Do not use the development install for production"}
`sudo make install -- -d` is a volatile development path. It can reset state and should not be used for production installs.
:::

## Recommended APT Install

Use the install script for the normal packaged install:

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash
```

For an interactive install with prompts for optional setup:

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash -s -- --interactive
```

From a checked-out repository, the same helper is available as:

```bash
./bin/vh/install.sh
./bin/vh/install.sh --interactive
```

## Manual APT Setup

If you prefer to add the repository yourself:

```bash
sudo curl -fsSL https://apt.vaulthalla.sh/pubkey.gpg -o /etc/apt/trusted.gpg.d/vaulthalla.gpg
echo "deb [arch=amd64] https://apt.vaulthalla.sh stable main" | sudo tee /etc/apt/sources.list.d/vaulthalla.list > /dev/null
sudo apt update
sudo apt install vaulthalla
```

## Install Profiles

The default package includes the core daemon, CLI, web runtime, systemd units, lifecycle utility, SQL assets, Nginx template, and recommended dependencies.

Use a lean install when the host already has the required services and you do not want recommended packages installed:

```bash
sudo apt install --no-install-recommends vaulthalla
```

Skip local database bootstrap during package install:

```bash
VH_SKIP_DB_BOOTSTRAP=1 sudo -E apt install vaulthalla
```

Skip Nginx setup during package install:

```bash
VH_SKIP_NGINX_CONFIG=1 sudo -E apt install vaulthalla
```

The repository helper also accepts install-time controls:

```bash
./bin/vh/install.sh --lean
./bin/vh/install.sh --no-db
./bin/vh/install.sh --no-nginx
./bin/vh/install.sh --assign-user <linux-user>
./bin/vh/install.sh --skip-admin-assign
```

## What The Package Creates

The package installs these main runtime pieces:

- `vaulthalla.service` for the core daemon.
- `vaulthalla-cli.socket` and `vaulthalla-cli.service` for the local CLI control socket.
- `vaulthalla-web.service` for the packaged web console.
- `vaulthalla-swtpm.service` when the software TPM fallback is needed.
- `/usr/bin/vh` and `/usr/bin/vaulthalla`, both pointing at the CLI.
- `/etc/vaulthalla/config.yaml` for runtime configuration.
- `/var/lib/vaulthalla` for Vaulthalla state.
- `/run/vaulthalla` for sockets and runtime secrets.
- `/mnt/vaulthalla` for the FUSE filesystem surface.

See [Runtime Paths](/reference/runtime-paths) for the full path map.

## TPM Or Software TPM

Vaulthalla needs TPM-compatible key protection. The package prefers a hardware TPM when `/dev/tpmrm0` or `/dev/tpm0` is available. If no hardware TPM is available, the managed `swtpm` service provides a local software TPM with state under `/var/lib/swtpm/vaulthalla`.

If neither hardware TPM nor `swtpm` is usable, configuration fails with a clear error. Use [Install Troubleshooting](/troubleshooting/install-troubleshooting) to diagnose TPM and `swtpm` failures.

## Local PostgreSQL Bootstrap

When local PostgreSQL is installed and healthy, package setup can create or reuse the `vaulthalla` role and database. If a database already exists, interactive package flows preserve it unless you explicitly choose destructive recreation.

You can also bootstrap later:

```bash
sudo vh setup db
```

For a remote database, use:

```bash
sudo vh setup remote-db --host <host> --port 5432 --user <user> --database <name> --password-file <path>
```

## Nginx And TLS

Package setup can configure Nginx when the host has Nginx active and the lifecycle checks are low risk. You can also configure it later:

```bash
sudo vh setup nginx --domain vault.example.com
sudo vh setup nginx --domain vault.example.com --certbot
sudo vh setup nginx --domain vaulthalla.dev --s3-domain s3.vaulthalla.dev --certbot-dns-cloudflare --cloudflare-credentials /etc/vaulthalla/certbot/cloudflare.ini
```

The Certbot option validates prerequisites and uses rollback behavior if certificate setup fails. The Cloudflare DNS-01 option issues a certificate without requiring an inbound HTTP challenge endpoint and renders a dedicated HTTPS S3 host.

## Verify The Install

After installation:

```bash
vh status
systemctl status vaulthalla.service
systemctl status vaulthalla-web.service
systemctl status vaulthalla-cli.socket
```

If the CLI reports a socket or permission error, finish [First Run](/getting-started/first-run), especially the admin Linux UID and `vaulthalla` group steps.

## Remove Or Purge

Remove the package while preserving most state:

```bash
sudo apt remove vaulthalla
```

Purge package-managed config:

```bash
sudo apt purge vaulthalla
```

Package purge does not silently destroy a preserved database. Interactive purge flows may offer database cleanup. Noninteractive purge preserves database state.

To intentionally tear down Vaulthalla-managed local database state:

```bash
sudo vh teardown db
```

To remove managed Nginx configuration:

```bash
sudo vh teardown nginx
```
