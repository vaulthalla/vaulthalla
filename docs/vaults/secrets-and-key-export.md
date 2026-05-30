---
title: Secrets And Key Export
description: Export vault keys and internal secrets safely, and seed database or JWT secrets from operator-controlled files.
order: 250
status: published
tags:
  - secrets
  - keys
  - recovery
---

# Secrets And Key Export

Vaulthalla separates vault encryption keys from internal runtime secrets. Export both before storing critical data and whenever your recovery plan changes.

:::toc[On this page]{depth="3" theme="compact"}
:::

## What To Export

| Material | Command family | Why it matters |
| --- | --- | --- |
| Vault keys | `vh vault keys export` | Required to recover encrypted vault content outside the original TPM context. |
| Database password | `vh secret export db-password` | Required to reconnect services to an existing PostgreSQL database. |
| JWT secret | `vh secret export jwt-secret` | Required to preserve token/session behavior across recovery where applicable. |
| All internal secrets | `vh secret export all` | Convenient recovery bundle for internal non-vault secrets. |

## Export Vault Keys

Export all vault keys with GPG encryption:

```bash
vh vault keys export all \
  --recipient <gpg-fingerprint> \
  --output vaulthalla-vault-keys.json.gpg
```

Export a single vault:

```bash
vh vault keys export archive \
  --recipient <gpg-fingerprint> \
  --output archive-vault-key.json.gpg
```

If you omit `--recipient`, Vaulthalla warns and writes unencrypted JSON to the output file or stdout.

## Export Internal Secrets

```bash
vh secret export all \
  --recipient <gpg-fingerprint> \
  --output vaulthalla-secrets.json.gpg
```

Single-secret examples:

```bash
vh secret export db-password --recipient <gpg-fingerprint> --output db-password.json.gpg
vh secret export jwt-secret --recipient <gpg-fingerprint> --output jwt-secret.json.gpg
```

When `--recipient` is used, provide `--output` so the encrypted result lands in a file.

## Seed Internal Secrets

Set the database password from a file:

```bash
vh secret set db-password /root/vaulthalla-db-password
```

Set the JWT secret from a file:

```bash
vh secret set jwt-secret /root/vaulthalla-jwt-secret
```

The CLI reads and trims the file contents. Protect the source file and remove temporary copies after use.

## Permissions

Key and secret exports require elevated Vaulthalla permissions. Super admins can export; other users need the appropriate encryption-key export permission.

Exports are audit-sensitive operations. Treat them as operational events, not routine casual diagnostics.

## Storage Rules

Use these rules for recovery material:

- Encrypt exports to an operator-controlled GPG recipient.
- Store exports outside the Vaulthalla host.
- Keep at least one offline or separately administered copy.
- Do not store key exports in the same bucket they protect unless additional independent encryption is applied.
- Document which export belongs to which Vaulthalla version and database backup.

## Import Boundary

The current operator-facing CLI documents export and rotation flows. Do not assume a one-command vault-key import workflow exists for disaster recovery. Keep database backups, Vaulthalla state, TPM or `swtpm` state, and encrypted exports together as a recovery set, and test the restore path before relying on it.
