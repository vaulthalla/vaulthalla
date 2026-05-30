---
title: Install Troubleshooting
description: Diagnose Vaulthalla package install, setup, database, TPM, swtpm, Nginx, Certbot, FUSE, and service failures.
order: 800
status: published
tags:
  - troubleshooting
  - install
  - operations
---

# Install Troubleshooting

Use this guide when the package install, first-run setup, services, database bootstrap, TPM setup, Nginx, Certbot, or FUSE mount does not behave as expected.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Start With Status

```bash
vh status
systemctl status vaulthalla.service
systemctl status vaulthalla-web.service
systemctl status vaulthalla-cli.socket
systemctl status vaulthalla-swtpm.service
```

Follow logs:

```bash
journalctl -fu vaulthalla.service
journalctl -fu vaulthalla-web.service
```

If you are working from a repository clone, the local doctor script can provide extra context:

```bash
bash .codex/scripts/doctor.sh
```

## APT Repository Or Package Fails

Check repository configuration:

```bash
ls -l /etc/apt/sources.list.d/vaulthalla.list
ls -l /etc/apt/trusted.gpg.d/vaulthalla.gpg
sudo apt update
apt-cache policy vaulthalla
```

If the package is partially configured:

```bash
sudo dpkg --configure -a
sudo apt -f install
```

Then rerun the relevant setup step instead of reinstalling blindly.

## CLI Cannot Connect

Check the socket:

```bash
ls -l /run/vaulthalla/cli.sock
systemctl status vaulthalla-cli.socket
systemctl status vaulthalla-cli.service
```

Check group membership:

```bash
id
getent group vaulthalla
```

If the user was just added to the group:

```bash
newgrp vaulthalla
```

or log out and back in.

## Admin UID Not Assigned

Run as the intended operator Linux user:

```bash
vh setup assign-admin
```

If the admin user is already bound to a different Linux UID, stop and decide whether the original binding is correct. Do not try to work around the check by changing random database rows.

## Database Bootstrap Fails

Check local PostgreSQL:

```bash
systemctl status postgresql
pg_isready
```

Run local setup:

```bash
sudo vh setup db
```

For remote PostgreSQL, validate the password file and network path:

```bash
sudo vh setup remote-db --host <host> --port 5432 --user <user> --database <name> --password-file <path>
```

Check that `/run/vaulthalla/db_password` exists when the service expects a runtime database password file:

```bash
sudo ls -l /run/vaulthalla/db_password
```

For a preserved database reinstall, reseed the runtime password file:

```bash
sudo install -d -m 0755 /run/vaulthalla
sudo install -m 0600 -o vaulthalla -g vaulthalla /path/to/db_password /run/vaulthalla/db_password
sudo systemctl restart vaulthalla
```

## TPM Or swtpm Fails

Check hardware TPM devices:

```bash
ls -l /dev/tpmrm0 /dev/tpm0
```

Check software TPM:

```bash
systemctl status vaulthalla-swtpm.service
journalctl -u vaulthalla-swtpm.service
```

Check the core daemon logs for TPM/TCTI errors:

```bash
journalctl -u vaulthalla.service
```

If the host has no hardware TPM, make sure `swtpm` and `swtpm-tools` are installed and the managed service can write `/var/lib/swtpm/vaulthalla`.

## Crypto Initialization Fails

If logs mention missing AES or PCLMUL support, the production host may not meet the AES-256-GCM runtime requirements. Move to supported hardware or a supported VM shape. Development overrides are not production fixes.

## Nginx Setup Fails

Validate Nginx:

```bash
sudo nginx -t
systemctl status nginx
```

Run setup:

```bash
sudo vh setup nginx --domain vault.example.com
```

If Certbot was requested:

```bash
sudo vh setup nginx --domain vault.example.com --certbot
```

Check for conflicting sites before rerunning. The lifecycle command manages the Vaulthalla site and should roll back failed low-risk changes.

## Web Service Fails

```bash
systemctl status vaulthalla-web.service
journalctl -fu vaulthalla-web.service
```

Confirm the core daemon is also running:

```bash
systemctl status vaulthalla.service
```

If Nginx serves an error page, verify both the web service and proxy configuration.

## FUSE Mount Fails

Check the mount:

```bash
mount | grep vaulthalla
ls -ld /mnt/vaulthalla
```

If the mount is stale after a crash or forced stop:

```bash
sudo fusermount3 -uz /mnt/vaulthalla
sudo systemctl restart vaulthalla.service
```

Check `/etc/fuse.conf` if your deployment requires `allow_other` behavior:

```bash
grep user_allow_other /etc/fuse.conf
```

## When To Stop

Stop and take a backup before destructive choices such as database teardown, package purge, or manual state deletion. If the host already contains production vault data, preserve PostgreSQL, `/etc/vaulthalla`, `/var/lib/vaulthalla`, and any `swtpm` state before continuing.
