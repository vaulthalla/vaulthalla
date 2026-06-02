---
title: S3 Gateway Credentials And Scopes
description: Manage inbound gateway credentials, scope modes, vault allowlists, and per-vault action flags for downstream S3 clients.
order: 30
status: published
tags:
  - s3-gateway
  - credentials
  - scopes
  - access-control
---

# S3 Gateway Credentials And Scopes

Gateway credentials are inbound downstream S3 credentials issued by Vaulthalla. They are not upstream provider credentials and should not reuse AWS, Cloudflare R2, or other provider keys.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Credential Types

| Credential | Used by | Purpose |
| --- | --- | --- |
| Gateway credential | Downstream S3 clients -> Vaulthalla | Authenticate AWS CLI, rclone, MinIO mc, SDKs, and apps to S3 Gateway. |
| Upstream provider credential | Vaulthalla -> upstream provider | Let Vaulthalla access an upstream S3/R2 bucket for an S3/R2 vault. Created with `vh api-key`. |

The gateway secret access key is shown only when created. It is encrypted at rest and later used by Vaulthalla for SigV4 verification.

## Scope Modes

Gateway credentials authenticate as an effective Vaulthalla principal user. The principal's normal RBAC must allow the action, and the credential scope must also allow it.

```text
allowed = principal RBAC allows the action
          and credential scope allows the bucket/vault/action
          and gateway budget preflight allows estimated cost
```

| Scope | Behavior |
| --- | --- |
| `user_access` | The key can access gateway buckets that the principal user can already access through normal Vaulthalla RBAC. |
| `vault_allowlist` | The key can access only listed vaults, with independent per-vault action flags. |
| `global` | Admin-created service credential for all gateway bucket bindings, still audited and tied to an admin principal. |

## Per-Vault Flags

`vault_allowlist` rows can grant these gateway actions per vault:

| Flag | S3 Gateway operations |
| --- | --- |
| `list` | List buckets and objects. |
| `read` | HEAD, GET, and metadata reads. |
| `write` | PUT, COPY destination writes, and multipart upload writes. |
| `delete` | Object delete and multi-delete. |
| `admin` | Bucket bind, unbind, create, delete, and administration paths. |

Scope flags never replace RBAC. A key that lists a vault still cannot access that vault unless the principal user has matching Vaulthalla permissions.

## CLI Examples

Create credentials:

```bash
# Personal full-access key, bounded by the user's normal vault permissions.
vh s3-gateway creds create laptop --scope user-access --json

# Single-bucket backup key.
vh s3-gateway creds create backup \
  --scope vault-allowlist \
  --vault archive \
  --list --read --write \
  --json

# Multi-vault read-only analytics key.
vh s3-gateway creds create analytics \
  --scope vault-allowlist \
  --vault photos \
  --vault reports \
  --list --read \
  --json

# Global admin service key. Requires admin permission.
vh s3-gateway creds create gateway-ops --scope global --user admin --json
```

Inspect, update, and revoke:

```bash
vh s3-gateway creds list
vh s3-gateway creds scope backup show
vh s3-gateway creds scope backup set --scope vault-allowlist
vh s3-gateway creds scope backup allow-vault archive --read --write
vh s3-gateway creds scope backup revoke-vault archive
vh s3-gateway creds revoke VH...
```

Local/cache budget accounting is off by default. Enable it only when a gateway credential should consume key budgets for pure local buckets, metadata-only requests, cache hits, and sync-deferred local-first writes/deletes:

```bash
vh s3-gateway creds create backup --enforce-budget-for-local-requests
vh s3-gateway creds scope backup set --enforce-budget-for-local-requests
vh s3-gateway creds scope backup set --no-enforce-budget-for-local-requests
```

## Web Console Workflow

In Admin -> S3 Gateway:

1. Open the credentials section.
2. Create a credential and choose the effective user, description, expiry, and scope mode.
3. Copy the secret access key immediately; it is revealed only once.
4. For `vault_allowlist`, select vaults and action flags.
5. Save scope edits after changing mode, user, expiry, description, or local budget enforcement.
6. Revoke keys that should no longer authenticate downstream S3 clients.

The web console exposes **Count local/cache hits against gateway request budgets** for the same `enforce_budget_for_local_requests` setting.
