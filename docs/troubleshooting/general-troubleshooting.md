---
title: General Troubleshooting
description: Diagnose common Vaulthalla CLI, web, vault, sync, S3/R2, encryption, sharing, cost-control, and backup issues.
order: 810
status: published
tags:
  - troubleshooting
  - operations
---

# General Troubleshooting

Use this guide after Vaulthalla is installed but an operator workflow is failing.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Fast Triage

```bash
vh status
systemctl status vaulthalla.service
systemctl status vaulthalla-web.service
journalctl -u vaulthalla.service -n 200
```

For vault sync:

```bash
vh vault sync info <vault>
vh vault sync dry-run <vault>
```

For permissions:

```bash
vh user info <user>
vh vault role list <vault>
vh permissions --type vault
```

## CLI Permission Error

Check:

```bash
id
getent group vaulthalla
ls -l /run/vaulthalla/cli.sock
```

If group membership was just changed, refresh the session with `newgrp vaulthalla` or log out and back in. Then confirm the Vaulthalla user has the expected Linux UID mapping.

## Web Login Works But Pages Are Empty

Check the core daemon and WebSocket path:

```bash
systemctl status vaulthalla.service
journalctl -u vaulthalla.service -n 200
sudo nginx -t
```

If the dashboard loads but filesystem or vault data does not, check the user's admin role, vault role, and group membership.

## Preview Or Download Fails

Check:

- The user or share has `preview` or `download` permission.
- The preview HTTP server is enabled.
- The core daemon can read the vault object.
- The Nginx preview route is proxying correctly.
- The file type can be previewed.

Fallback test:

```bash
vh vault sync info <vault>
```

Then try downloading the file through a user with known vault access.

## S3/R2 Vault Cannot Connect

Inspect the API key and vault:

```bash
vh api-key info <key>
vh vault info <vault>
vh vault sync dry-run <vault> --refresh-index
```

Check:

- Endpoint URL is correct.
- Provider value matches the service.
- Bucket exists.
- Credentials allow required bucket/object operations.
- Region is correct or `auto` is appropriate.
- Request and price budgets are not blocking the operation.

## Sync Stalled

A stalled sync is often protective behavior. Start with:

```bash
vh vault sync info <vault>
vh vault sync dry-run <vault>
```

Common stall causes:

- Request budget exceeded.
- Price budget enforcement denied the run.
- Remote index is stale.
- Manifest conflict repeated.
- Archive-tier object needs provider restore.
- Missing encryption metadata for an encrypted remote object.
- Conflict policy requires operator decision.

Raise only the limit or fix only the condition named in the stall reason.

## Remote Index Is Stale

Options:

```bash
vh vault sync dry-run <vault> --refresh-index
vh vault sync inventory <vault> --file inventory.csv
vh vault sync events <vault> --file s3-events.json
vh vault sync reconcile <vault> --allow-list-scan
```

For large buckets, prefer inventory plus event ingestion over repeated full scans.

## Cost Control Blocks Work

Request budget:

```bash
vh vault sync info <vault>
vh vault sync dry-run <vault>
vh vault sync set <vault> --s3-budget-get 2000
```

Price budget:

```bash
vh pricing budget status
vh pricing budget ledger --limit 50
vh pricing budget set-vault <vault> --mode warn --max-run 1
```

Do not switch to unlimited until a dry-run proves the operation is intentional and bounded by some other control.

## Encrypted S3 Object Cannot Be Read

Check:

- Upstream object metadata includes Vaulthalla encryption metadata.
- The vault key version exists.
- The remote index is current.
- The object was not overwritten outside Vaulthalla without preserving metadata.
- The vault was not switched between encrypted and unencrypted behavior without a migration plan.

If key material may be missing, stop and preserve current PostgreSQL, `/var/lib/vaulthalla`, bucket data, and any key exports before attempting repair.

## Share Link Does Not Work

Check:

- The share is still enabled.
- The URL was copied at create or rotate time.
- The recipient completed email validation if required.
- Operator email is healthy for email-validated shares.
- The share role includes the needed operation.
- The underlying vault object still exists.

Email checks:

```bash
vh email doctor
vh email history --limit 100
```

## Backup Readiness Looks Wrong

Dashboard backup/recovery indicators are status and policy signals. They do not prove that a database dump, state archive, key export, or bucket backup has run.

Use [Backup And Recovery](/vaults/backup-and-recovery) to build and test a real recovery set.

## Before Manual Repair

Before direct database edits, state deletion, or bucket rewrites:

1. Stop affected services if needed.
2. Back up PostgreSQL.
3. Back up `/etc/vaulthalla`.
4. Back up `/var/lib/vaulthalla`.
5. Back up `/var/lib/swtpm/vaulthalla` if using software TPM.
6. Export vault keys and internal secrets where possible.

Manual repair without recovery material can convert a recoverable fault into data loss.
