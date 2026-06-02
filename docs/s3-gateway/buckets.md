---
title: S3 Gateway Buckets
description: Create and bind local and remote-backed gateway buckets that expose Vaulthalla vaults through the S3 protocol.
order: 40
status: published
tags:
  - s3-gateway
  - buckets
  - vaults
  - storage
---

# S3 Gateway Buckets

A gateway bucket is a downstream S3 bucket name exposed by Vaulthalla and bound to a Vaulthalla vault. It is the name downstream S3 clients use in commands such as `s3://archive/path/file.txt`.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Bucket Modes

| Mode | Vault backing | Behavior |
| --- | --- | --- |
| `local` | Local Vaulthalla vault | Encrypted local object store exposed through S3. |
| `remote_cache` | Vaulthalla S3/R2 vault | Proxy/cache surface over an upstream S3/R2 bucket, using cache sync behavior. |
| `remote_proxy` | Vaulthalla S3/R2 vault | Proxy/cache surface over an upstream S3/R2 bucket, using proxy-oriented remote-backed behavior. |

Local vaults must be bound as `local`. S3/R2 vaults must be bound as `remote_cache` or `remote_proxy`.

## Local Gateway Buckets

Create a new encrypted local vault and bind it as a gateway bucket:

```bash
vh s3-gateway bucket create-local archive --quota 500G
```

Creating a local gateway bucket creates a normal Vaulthalla local vault. The actor must have normal vault-create permission for the target owner. Creating a bucket for another owner follows the same target-user vault-create rules as ordinary vault creation and does not bypass Vaulthalla RBAC.

Bind an existing local vault:

```bash
vh s3-gateway bucket bind photos --vault 12 --mode local
vh s3-gateway bucket list
```

Local gateway buckets support standard object operations such as PUT, GET, HEAD, LIST, COPY, multipart upload, and DELETE while Vaulthalla stores encrypted local content.

## Remote-Backed Gateway Buckets

A remote-backed gateway bucket is a downstream S3 bucket backed by a Vaulthalla S3/R2 vault. Downstream clients talk to Vaulthalla; Vaulthalla uses its upstream provider credential and upstream S3/R2 bucket behind that vault.

Bind an existing S3/R2 vault:

```bash
vh s3-gateway bucket bind archive-edge --vault archive --mode remote_cache
vh s3-gateway bucket bind archive-proxy --vault archive --mode remote_proxy
```

Binding an existing vault requires authority to manage that vault and S3 Gateway bucket-management authority. Binding does not grant object access to gateway credentials; each credential still needs its own gateway vault-role assignment.

Create a new remote-cache gateway bucket and its backing S3/R2 vault:

```bash
vh s3-gateway bucket create-remote-cache edge \
  --api-key r2-main \
  --upstream-bucket origin \
  --encrypt
```

`--api-key` names the upstream provider credential created with `vh api-key`. `--upstream-bucket` names the external AWS S3, Cloudflare R2, or compatible bucket used as the vault backend.

Creating a remote-cache bucket also creates a normal S3/R2 vault, so it requires vault-create permission for the owner and permission to use the upstream provider credential.

## API-Exclusive Buckets

API-exclusive buckets are intended for vaults created and owned primarily through S3 Gateway. They keep downstream S3 client expectations clearer by reducing unrelated Vaulthalla workflows that might create conflicting object state in the same vault.

Use API-exclusive local buckets for MinIO-like local object storage. Use API-exclusive remote-cache buckets when Vaulthalla should expose an S3-compatible proxy/cache edge while relying on the remote object index, manifest sync, and configured upstream provider bucket.

## Backfill

Backfill imports existing Vaulthalla metadata into gateway object state:

```bash
vh s3-gateway bucket backfill archive
vh s3-gateway bucket backfill archive --calculate-etags
```

The default backfill is metadata-only. `--calculate-etags` intentionally reads and decrypts local object bodies to calculate plaintext MD5 ETags and is only supported for local Vaulthalla files.
