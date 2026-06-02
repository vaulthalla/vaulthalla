---
title: S3 Gateway Credentials And Scopes
description: Manage inbound gateway credentials, effective principals, vault roles, and path overrides for downstream S3 clients.
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

Gateway credentials authenticate as an effective Vaulthalla principal user. The principal's normal RBAC must allow the action, and the gateway credential's own vault role assignment must also allow it.

```text
allowed = principal RBAC allows the action
          and credential vault role allows the bucket/vault/path/action
          and gateway budget preflight allows estimated cost
```

| Scope | Behavior |
| --- | --- |
| `user_access` | The key defaults to no gateway vault-role assignments. Grant explicit credential roles before using it for bucket/object access. |
| `vault_allowlist` | The key can access only listed vaults with gateway credential vault-role assignments and optional path overrides. Legacy `--list --read --write --delete --admin` shorthands are converted into role assignments. |
| `global` | Admin-created service credential for all gateway bucket bindings, still audited and tied to an admin principal. |

:::callout[Secure default]{theme="warning"}
Existing boolean scope rows are migrated into gateway credential vault-role assignments where possible. Credentials without a migrated or explicit role assignment are denied by default, even when the principal user has normal vault access.
:::

## Vault Roles And Overrides

Each gateway credential can have one enabled vault-role assignment per vault. The assigned vault role uses the same filesystem permission model as ordinary Vaulthalla vault roles, and optional path/glob overrides can allow or deny individual filesystem permissions.

| S3 action | RBAC-style permission gate |
| --- | --- |
| List buckets, head bucket, list objects | Vault list permission on the bucket root or requested prefix. |
| HEAD/GET object and copy source | Vault read permission on the object path. |
| PUT, copy destination, multipart write/complete | Vault write or overwrite permission on the object path. |
| Delete object and multi-delete | Vault delete permission on each object path. |

Deny overrides win over role grants. The principal user's normal RBAC is always evaluated separately, so a gateway key role cannot widen the user's own Vaulthalla authority.

## Principal Assignment

Non-admin users can create gateway credentials only for themselves. Creating or updating a credential whose principal differs from the actor requires the explicit `admin.s3_gateway.assign_principal` permission; super-admins have it through the built-in full S3 Gateway permission set.

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

The `allow-vault` shorthand is a compatibility path. At save time, Vaulthalla converts it into a gateway credential vault-role assignment such as `reader`, `contributor`, or `manager`.

Assign explicit gateway credential vault roles and path overrides:

```bash
vh s3-gateway creds role assign backup --vault archive --role reader
vh s3-gateway creds role list backup
vh s3-gateway creds role override add backup \
  --vault archive \
  --pattern "/exports/private/*" \
  --permission download \
  --effect deny
vh s3-gateway creds role override list backup --vault archive
vh s3-gateway creds role override remove backup --vault archive 42
vh s3-gateway creds role revoke backup --vault archive
```

The role commands write `s3_gateway_credential_vault_role_assignment` and `s3_gateway_credential_vault_role_override` rows directly. They also keep the deprecated boolean scope row aligned for older scope display commands, but request authorization uses the role assignment and override tables.

Local/cache budget accounting is off by default. Enable it only when a gateway credential should consume key budgets for pure local buckets, metadata-only requests, cache hits, and sync-deferred local-first writes/deletes:

```bash
vh s3-gateway creds create backup --enforce-budget-for-local-requests
vh s3-gateway creds scope backup set --enforce-budget-for-local-requests
vh s3-gateway creds scope backup set --no-enforce-budget-for-local-requests
```

## Web Console Workflow

In Admin -> S3 Gateway:

1. Open the credentials section.
2. Create a credential and choose the effective principal, description, expiry, and scope mode.
3. Copy the secret access key immediately; it is revealed only once.
4. For `vault_allowlist`, select vaults and vault roles.
5. Save scope edits after changing mode, user, expiry, description, or local budget enforcement.
6. Revoke keys that should no longer authenticate downstream S3 clients.

The web console exposes **Count local/cache hits against gateway request budgets** for the same `enforce_budget_for_local_requests` setting.
