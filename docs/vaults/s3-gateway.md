---
title: Vaults Exposed Through S3 Gateway
description: Understand how Vaulthalla vaults are mapped to downstream S3 Gateway buckets without treating the gateway as a vault type.
order: 240
status: published
tags:
  - vaults
  - s3-gateway
  - storage
---

# Vaults Exposed Through S3 Gateway

S3 Gateway is not a vault type. It is a downstream S3-compatible protocol surface that exposes Vaulthalla vaults as gateway buckets.

For complete setup, credentials, client examples, budget controls, and protocol semantics, see [S3 Gateway](/s3-gateway).

:::toc[On this page]{depth="3" theme="compact"}
:::

## Mapping

| S3 Gateway concept | Vaulthalla concept |
| --- | --- |
| Gateway bucket | S3 bucket name exposed by Vaulthalla and bound to a vault. |
| Object key | Vault-relative path. |
| Gateway credential | Inbound credential for downstream S3 clients. |
| Bucket mode | Binding behavior for local or remote-backed vaults. |

Downstream S3 clients such as AWS CLI, rclone, MinIO mc, SDKs, and backup tools talk to Vaulthalla. They do not talk directly to the vault's upstream provider bucket.

## Local Vaults Through S3

A local vault plus S3 Gateway behaves like an encrypted local object store exposed through an S3-compatible endpoint:

```bash
vh s3-gateway bucket create-local archive
aws configure set s3.addressing_style path
aws --endpoint-url https://s3.vaulthalla.example.com s3 cp ./report.pdf s3://archive/reports/report.pdf
```

Vaulthalla stores encrypted local content while downstream S3 clients operate with bucket and object-key paths.

## S3/R2 Vaults Through S3 Gateway

An S3/R2 vault already uses an upstream S3/R2 bucket as storage. When that vault is exposed through S3 Gateway, downstream S3 clients still connect to Vaulthalla:

```text
downstream S3 client -> Vaulthalla S3 Gateway -> Vaulthalla S3/R2 vault -> upstream S3/R2 bucket
```

This remote-backed gateway bucket acts as a Vaulthalla-controlled proxy/cache surface over the vault's upstream provider bucket. Gateway PUT and DELETE are local-first in Vaulthalla, and sync owns eventual upstream provider side effects.

For upstream S3/R2 vault configuration, see [S3 And R2 Vaults](/vaults/s3-r2-vaults). For gateway bucket creation and binding, see [S3 Gateway Buckets](/s3-gateway/buckets).

## Related Reading

- [S3 Gateway](/s3-gateway) for the canonical gateway guide.
- [S3 Gateway Clients](/s3-gateway/clients) for AWS CLI, rclone, and MinIO mc.
- [S3 And R2 Vaults](/vaults/s3-r2-vaults) for upstream provider storage.
- [Local Vaults](/vaults/local-vaults) for local encrypted vault storage.
