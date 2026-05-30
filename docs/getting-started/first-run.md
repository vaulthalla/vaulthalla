---
title: First Run
description: Bootstrap Vaulthalla after installation, bind the CLI admin Linux UID, configure database access, and expose the web console.
order: 20
status: published
tags:
  - setup
  - cli
  - admin
---

# First Run

After installation, finish the host-specific setup: database access, CLI ownership, group membership, service health, and optional Nginx/TLS exposure.

:::toc[On this page]{depth="3" theme="compact"}
:::

## First-Run Checklist

:::steps
1. Confirm the package installed and services exist with `vh status` and `systemctl status`.
2. Bootstrap a local or remote PostgreSQL connection.
3. Add the operator Linux user to the `vaulthalla` group.
4. Bind the initial CLI admin user to that Linux UID.
5. Configure Nginx and TLS if the web console should be reachable by domain.
6. Open the web console and create the first operational vault.
:::

## Database Setup

For local PostgreSQL:

```bash
sudo vh setup db
```

For remote PostgreSQL:

```bash
sudo vh setup remote-db \
  --host db.example.com \
  --port 5432 \
  --user vaulthalla \
  --database vaulthalla \
  --password-file /root/vaulthalla-db-password
```

Use `VH_SKIP_DB_BOOTSTRAP=1` during package install when you want to configure database access after the package is installed.

## CLI Admin Linux UID

Vaulthalla's local CLI is intentionally tied to Linux identity. The CLI talks to `/run/vaulthalla/cli.sock`, which is owned for Vaulthalla use. Non-root CLI users must be in the `vaulthalla` group and must be mapped to an application user by Linux UID.

The initial admin binding is handled by:

```bash
vh setup assign-admin
```

Run this as the Linux account that should operate Vaulthalla. The command binds the built-in admin user to that Linux UID when it is still unbound. If the admin user is already bound to the same UID, the command reports that it is already assigned. If it is bound to a different UID, it refuses to rebind.

:::callout{theme="warning" title="The first non-root CLI user can become the CLI owner"}
If setup is skipped and the admin UID is still unbound, the first eligible non-root user that reaches the CLI socket can claim the initial CLI ownership. Choose the operator user intentionally during install or immediately run `vh setup assign-admin` as the intended user.
:::

## Group Membership

The installer can add an operator to the `vaulthalla` group. If you do it manually:

```bash
sudo usermod -aG vaulthalla <linux-user>
```

Then refresh the login session:

```bash
newgrp vaulthalla
```

or log out and back in. Confirm membership:

```bash
id
getent group vaulthalla
```

## Lifecycle Commands Require Sudo

Runtime lifecycle commands modify host services or privileged configuration and must be run with `sudo`:

```bash
sudo vh setup db
sudo vh setup remote-db --host <host> --user <user> --database <name> --password-file <path>
sudo vh setup nginx --domain vault.example.com
sudo vh teardown nginx
sudo vh teardown db
```

Most day-to-day commands, such as `vh vaults`, `vh user`, `vh api-key`, and `vh vault sync`, are normal CLI commands that use the local control socket.

For unattended lifecycle automation, set:

```bash
VAULTHALLA_NONINTERACTIVE=1
```

or pass lifecycle helper options such as `--non-interactive` or `--yes` where supported.

## Configure The Web Console

If Nginx was not configured during package install:

```bash
sudo vh setup nginx --domain vault.example.com
```

For managed TLS:

```bash
sudo vh setup nginx --domain vault.example.com --certbot
```

Then check:

```bash
systemctl status vaulthalla-web.service
systemctl status vaulthalla.service
sudo nginx -t
```

The web service listens locally and is intended to be exposed through Nginx.

## Health Checks

Use these after any setup change:

```bash
vh status
journalctl -fu vaulthalla.service
journalctl -fu vaulthalla-web.service
```

Check the CLI socket when `vh` cannot connect:

```bash
ls -l /run/vaulthalla/cli.sock
systemctl status vaulthalla-cli.socket
systemctl status vaulthalla-cli.service
```

## Next Steps

Create an API key if you plan to use S3/R2 vaults, create a vault, and then review backup and key export procedures before putting important data into the system:

- [S3 And R2 Vaults](/vaults/s3-r2-vaults)
- [Local Vaults](/vaults/local-vaults)
- [Backup And Recovery](/vaults/backup-and-recovery)
