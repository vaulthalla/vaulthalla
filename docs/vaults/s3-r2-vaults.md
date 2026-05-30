---
title: S3 And R2 Vaults
description: Create and operate S3-compatible Vaulthalla vaults using AWS S3, Cloudflare R2, or compatible providers.
order: 220
status: published
tags:
  - vaults
  - s3
  - r2
---

# S3 And R2 Vaults

S3-compatible vaults store file bodies in a bucket and use Vaulthalla metadata, sync policy, remote indexes, request budgets, and optional upstream encryption to keep local and remote state coherent.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Provider Credentials

Create an API key before creating the vault:

```bash
vh api-key create r2-main \
  --access <access-key> \
  --secret <secret-key> \
  --provider cloudflare-r2 \
  --endpoint https://<account-id>.r2.cloudflarestorage.com
```

AWS-style example:

```bash
vh api-key create aws-main \
  --access <access-key> \
  --secret <secret-key> \
  --provider aws \
  --endpoint https://s3.amazonaws.com \
  --region us-east-1
```

The endpoint is required. The default region is `auto`, which is suitable for Cloudflare R2 and some compatible providers.

Vaulthalla validates credentials during API key creation. Some providers return `AccessDenied` for broad bucket-list checks even when the credentials are valid for the intended bucket, so bucket-specific permissions should still be tested with a vault dry-run.

## Minimum Bucket Permissions

Grant only the bucket operations your sync policy needs. Typical S3/R2 vault operation may require:

- List bucket for reconcile and some remote-index refresh paths.
- Head/Get object for remote metadata and downloads.
- Put object for uploads and manifest publishes.
- Delete object when deletes are part of the sync policy.
- Copy object for provider-side copy paths where used.

Use request budgets and dry-runs before enabling broad remote operations on large buckets.

## Create

```bash
vh vault create archive \
  --s3 \
  --api-key r2-main \
  --bucket vaulthalla-archive \
  --sync-strategy cache \
  --on-sync-conflict keep_local \
  --encrypt
```

Useful options:

| Option | Purpose |
| --- | --- |
| `--api-key` | API key name or id. |
| `--bucket` | Bucket name. |
| `--storage-tier` or `--storage-class` | Provider storage class such as `standard`, `standard_ia`, or `infrequent_access`. |
| `--sync-strategy` | `cache`, `sync`, or `mirror`. |
| `--on-sync-conflict` | S3 conflict policy: `keep_local`, `keep_remote`, or `ask`. |
| `--encrypt` | Encrypt upstream object bodies before upload. |
| `--no-encrypt` | Store upstream object bodies without Vaulthalla encryption. |
| `--interval` | Sync interval. |
| `--owner` | Create for a specific owner where permitted. |

## Upstream Encryption

New S3/R2 vaults should normally use upstream encryption. With upstream encryption enabled, object payloads written by Vaulthalla are encrypted before upload and carry Vaulthalla metadata such as encryption status, IV, key version, and content hash.

With `--no-encrypt`, object payloads written upstream are plaintext from Vaulthalla's perspective. This can be useful for interoperability, but it changes the risk model. See [Encryption](/vaults/encryption) before disabling upstream encryption.

When changing encryption behavior on an existing bucket, Vaulthalla may require explicit waivers:

```bash
vh vault update archive --no-encrypt --accept-decryption-waiver
vh vault update archive --encrypt --accept-overwrite-waiver
```

Read the prompt and understand whether existing objects will remain as-is, be overwritten, or need a planned migration.

## Sync Strategies

| Strategy | Behavior |
| --- | --- |
| `cache` | Keep a local cache and index remote-only objects without downloading every body up front. Bodies are fetched as needed. |
| `sync` | Two-way synchronization between local and remote state. |
| `mirror` | Local-to-remote mirror behavior. Remote changes are not treated as authoritative local changes. |

See [Sync](/vaults/sync) before enabling a strategy on an existing bucket.

## Storage Tiers

Use storage tier options to request a provider storage class for uploaded objects. Examples include AWS `standard` or `standard_ia`, and Cloudflare R2 `standard` or `infrequent_access`.

Archive-tier objects may be indexed but skipped for body download until restored by the provider. Sync dry-run output reports archive-tier download skips when they are detected.

## Web Console

The Vaults page exposes the same major S3/R2 settings:

- API key and bucket.
- Storage tier.
- Sync strategy and conflict policy.
- Sync interval.
- Upstream encryption.
- Request budget preset or custom limits.
- Maximum remote-index age.
- Sync enabled state.

Use the web console for interactive setup and `vh vault sync info <vault>` for a scriptable view of the resulting policy.
