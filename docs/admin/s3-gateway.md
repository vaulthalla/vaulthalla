---
title: S3 Gateway Administration
description: Manage S3 Gateway service status, credentials, bucket bindings, budgets, and client snippets from the web console or CLI.
order: 30
status: published
tags:
  - admin
  - s3-gateway
  - operator
---

# S3 Gateway Administration

S3 Gateway is documented as a top-level protocol surface in [S3 Gateway](/s3-gateway). This administration page focuses on the management workflows operators use after deciding to expose Vaulthalla vaults to downstream S3 clients.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Web Console

Navigate to Admin -> S3 Gateway.

Operators can use the page to:

- Check service status, readiness, configured endpoint, and request counters.
- Create gateway credentials for downstream S3 clients.
- Copy the secret access key immediately after creation; it is shown only once.
- Choose `user_access`, `vault_allowlist`, or `global` scope.
- Edit credential scope, expiry, description, local/cache budget enforcement, gateway credential vault-role assignments, and path overrides.
- Create local gateway buckets and bind existing local or S3/R2 vaults.
- Save, disable, inspect, and troubleshoot gateway key and key/vault budgets.
- Review recent budget ledger rows and budget status.
- Copy client snippets for AWS CLI and MinIO mc.

Use [Credentials And Scopes](/s3-gateway/credentials-and-scopes), [Buckets](/s3-gateway/buckets), and [Cost Controls](/s3-gateway/cost-controls) for the full model behind these controls.

Gateway authorization is RBAC-native: the principal user's Vaulthalla RBAC is always the ceiling, and `vault_allowlist` credentials use gateway credential vault roles and overrides as the aperture. Assigning a gateway credential to a principal other than the actor requires `admin.s3_gateway.assign_principal`. Local gateway bucket creation remains self-service for the actor when they have normal vault-create permission for themselves; creating for another owner requires S3 Gateway bucket-management authority plus vault-create authority for that owner.

## CLI Equivalents

Service:

```bash
vh s3-gateway status
vh s3-gateway enable
vh s3-gateway disable
```

Credentials:

```bash
vh s3-gateway creds create laptop --json
vh s3-gateway creds list
vh s3-gateway creds revoke VH...
vh s3-gateway creds scope backup show
vh s3-gateway creds scope backup set --scope vault-allowlist
vh s3-gateway creds role assign backup --vault archive --role reader
vh s3-gateway creds role override add backup --vault archive --pattern "/private/*" --permission download --effect deny
vh s3-gateway creds role override list backup --vault archive
vh s3-gateway creds role override remove backup --vault archive 42
vh s3-gateway creds role revoke backup --vault archive
```

`vh s3-gateway creds scope allow-vault` and boolean create flags remain compatibility shorthand only. They are converted into gateway credential vault-role assignments; the role commands are the primary management model.

Buckets:

```bash
vh s3-gateway bucket list
vh s3-gateway bucket bind photos --vault 12 --mode local
vh s3-gateway bucket create-local archive
vh s3-gateway bucket create-remote-cache edge --api-key r2-main --upstream-bucket origin --encrypt
vh s3-gateway bucket backfill archive
```

Budgets:

```bash
vh s3-gateway budget set-key backup --monthly 5 --mode enforce --currency USD
vh s3-gateway budget set-key-vault backup --vault archive --monthly 2 --mode enforce
vh s3-gateway budget list --key backup
vh s3-gateway budget disable-key backup
vh s3-gateway budget disable-key-vault backup --vault archive
vh s3-gateway budget status --key backup
vh s3-gateway budget ledger --key backup --limit 50
```

## Related Guides

- [S3 Gateway Setup](/s3-gateway/setup) covers service enablement, direct listener access, managed Nginx S3-domain endpoints, and path-style SigV4 behavior.
- [S3 Gateway Clients](/s3-gateway/clients) shows AWS CLI, rclone, and MinIO mc examples for downstream S3 access.
- [S3 Gateway Semantics](/s3-gateway/semantics) explains LIST, HEAD, GET, PUT, DELETE, ETags, multipart uploads, backfill, and limitations.
