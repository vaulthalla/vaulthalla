---
title: Backup And Recovery
description: Build a Vaulthalla disaster recovery set from PostgreSQL, config, state, TPM or swtpm material, vault keys, internal secrets, and object storage.
order: 260
status: published
tags:
  - backup
  - recovery
  - operations
---

# Backup And Recovery

Backups for Vaulthalla must cover metadata, file bodies, configuration, key protection state, and exported recovery material. A PostgreSQL dump alone is not a complete backup.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Recovery Set

A practical recovery set includes:

| Item | Why it matters |
| --- | --- |
| PostgreSQL database dump | Users, groups, roles, vault definitions, sync policy, shares, audit state, key records, remote indexes, and metadata. |
| `/etc/vaulthalla/config.yaml` | Runtime ports, auth settings, pricing settings, sharing settings, cache policy, and service configuration. |
| `/var/lib/vaulthalla` | Vaulthalla-managed state, local vault bodies, cache/index state, and sealed key blobs. |
| `/var/lib/swtpm/vaulthalla` | Software TPM state when `swtpm` is used. |
| Vault key export | Portable recovery material for encrypted vault content. |
| Internal secret export | Database and JWT secrets managed by Vaulthalla. |
| S3/R2 bucket data | Remote object bodies for S3-compatible vaults. |
| Package version record | Helps restore with the same or compatible schema and runtime behavior. |

:::callout{theme="warning" title="No single backup command covers everything"}
The current operator surfaces expose key and secret export commands, but a full backup still needs database, config, state, and object-storage backups using your normal infrastructure tooling.
:::

## Recommended Backup Command Set

Create a PostgreSQL custom-format dump for a local database:

```bash
sudo -u postgres pg_dump -Fc vaulthalla > vaulthalla-db.dump
```

For remote PostgreSQL, use your database host, credentials, and network path:

```bash
pg_dump -h <host> -U <user> -Fc vaulthalla > vaulthalla-db.dump
```

Archive config and state:

```bash
sudo tar -C / -czf vaulthalla-config-state.tgz \
  etc/vaulthalla \
  var/lib/vaulthalla
```

If using `swtpm`, include software TPM state:

```bash
sudo tar -C / -czf vaulthalla-swtpm-state.tgz var/lib/swtpm/vaulthalla
```

Export vault keys:

```bash
vh vault keys export all \
  --recipient <gpg-fingerprint> \
  --output vaulthalla-vault-keys.json.gpg
```

Export internal secrets:

```bash
vh secret export all \
  --recipient <gpg-fingerprint> \
  --output vaulthalla-secrets.json.gpg
```

Record the package version:

```bash
vh version > vaulthalla-version.txt
```

## Local Vault Backup

For local vaults, the file bodies live in Vaulthalla-managed local state. Back up `/var/lib/vaulthalla` consistently with the PostgreSQL dump. If the system is active during backup, use snapshots or a maintenance window so the database and file bodies represent a coherent point in time.

## S3/R2 Vault Backup

For S3/R2 vaults, back up the bucket according to your provider's tooling. If upstream encryption is enabled, the remote object bodies still require Vaulthalla keys and metadata to decrypt.

Do not rely only on the provider bucket as a complete Vaulthalla backup. You still need PostgreSQL metadata, vault key exports, internal secrets, and configuration.

## Hardware TPM Recovery

Hardware TPM-sealed material is tied to the original TPM context. For host replacement, exported vault keys and internal secrets are the portable recovery material. Plan migration and restore testing before a hardware failure.

## Software TPM Recovery

When using `swtpm`, restoring `/var/lib/swtpm/vaulthalla` can preserve the software TPM context. Protect this directory like key material. Restoring it to a different host should be treated as a sensitive security event.

## Restore Outline

Use this as a cautious outline, not a blind script:

1. Install the same Vaulthalla version where possible.
2. Stop Vaulthalla services.
3. Restore `/etc/vaulthalla` and `/var/lib/vaulthalla`.
4. Restore `/var/lib/swtpm/vaulthalla` if the backup used software TPM.
5. Restore PostgreSQL from the dump.
6. Reseed or verify the database password secret if the database connection changed.
7. Start services.
8. Run `vh status`.
9. Run `vh vault sync info <vault>` for S3/R2 vaults.
10. Test read access to a known file in each vault.

Example service boundary:

```bash
sudo systemctl stop vaulthalla.service vaulthalla-web.service vaulthalla-cli.socket
sudo systemctl start vaulthalla.service vaulthalla-web.service vaulthalla-cli.socket
```

## Preserved Database Reinstall

If the package was removed but the database was preserved, reseed the runtime database password file before restart when needed:

```bash
sudo install -d -m 0755 /run/vaulthalla
sudo install -m 0600 -o vaulthalla -g vaulthalla /path/to/db_password /run/vaulthalla/db_password
sudo systemctl restart vaulthalla
```

## Test Restores

Run restore drills. A backup is not proven until an operator has restored:

- Database metadata.
- At least one local vault file.
- At least one S3/R2 vault file.
- An encrypted upstream object.
- A user, group, role, and share record.
- Cost-control policy state.

Document the exact Vaulthalla version, package source, database version, host TPM mode, and GPG recipient used for each recovery set.
