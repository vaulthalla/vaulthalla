---
title: S3 Gateway
navTitle: S3 Gateway
description: Expose Vaulthalla vaults through an S3-compatible endpoint for AWS CLI, rclone, MinIO mc, SDKs, and backup tools.
order: 260
status: published
tags:
  - s3-gateway
  - protocol
  - storage
  - operator
---

# S3 Gateway

S3 Gateway is Vaulthalla's S3-compatible protocol surface. It is not a vault type. A gateway bucket is an S3 bucket name exposed by Vaulthalla and bound to a Vaulthalla vault, and an S3 object key maps to a vault-relative path.

Downstream S3 clients such as AWS CLI, rclone, MinIO mc, SDKs, and backup applications connect to Vaulthalla's gateway endpoint with a gateway credential. Those inbound credentials are separate from upstream provider credentials created with `vh api-key` for Vaulthalla to access AWS S3, Cloudflare R2, or another compatible provider.

:::toc[On this page]{depth="3" theme="compact"}
:::

## How It Fits

| Concept | Meaning |
| --- | --- |
| Downstream S3 client | AWS CLI, rclone, MinIO mc, SDKs, or apps talking to Vaulthalla. |
| Gateway credential | Inbound S3 access key and secret issued by Vaulthalla for downstream clients. |
| Gateway bucket | S3 bucket name exposed by Vaulthalla and bound to a Vaulthalla vault. |
| Upstream S3/R2 bucket | External AWS S3, Cloudflare R2, or compatible bucket that Vaulthalla uses as storage for an S3/R2 vault. |
| Upstream provider credential | `vh api-key` credential used by Vaulthalla to talk to the upstream provider. |

Local gateway buckets expose encrypted local Vaulthalla vaults through S3. Remote-backed gateway buckets expose Vaulthalla S3/R2 vaults to downstream S3 clients, acting as a Vaulthalla-controlled proxy/cache surface over the vault's upstream S3/R2 bucket.

## Operation Model

- Buckets map to vaults.
- Object keys map to vault-relative paths.
- LIST is metadata-only; it does not decrypt bodies, compute plaintext ETags, refresh manifests, or call upstream S3/R2.
- For remote-backed gateway buckets, PUT and DELETE are local-first. The gateway mutates Vaulthalla state immediately, and sync owns eventual upstream provider side effects.
- GET can be local/cache served or can download a remote-only object when the vault policy allows it.
- Gateway key and key/vault budgets can account for actual upstream use and, when enabled per credential, synthetic local/cache request usage.

## Endpoints

Vaulthalla supports two gateway endpoint shapes:

| Endpoint | Typical URL | Use |
| --- | --- | --- |
| Direct listener | `http://127.0.0.1:39000` | Local tools, development, private host access, and smoke validation. |
| Managed S3-domain endpoint | `https://s3.vaulthalla.example.com` | Public or LAN reverse-proxy access through managed Nginx. |

The managed S3-domain endpoint uses path-style public reverse-proxy mode. Downstream clients should generally enable path-style addressing so requests sign and send ordinary S3 paths such as `/archive/reports/file.txt`.

## Guides

:::cards{columns="3" cardTheme="muted"}
:::card[Setup]{href="/s3-gateway/setup" linkScope="title"}
Enable the service, configure the listener, publish the managed Nginx S3 domain, and follow web console or CLI workflows.
:::

:::card[Clients]{href="/s3-gateway/clients" linkScope="title"}
Connect AWS CLI, rclone, and MinIO mc, including directory uploads, downloads, deletes, and multipart behavior.
:::

:::card[Credentials And Scopes]{href="/s3-gateway/credentials-and-scopes" linkScope="title"}
Create inbound gateway credentials, choose scope modes, grant per-vault action flags, and avoid upstream key reuse.
:::

:::card[Buckets]{href="/s3-gateway/buckets" linkScope="title"}
Create local gateway buckets, bind existing vaults, and expose remote-backed S3/R2 vaults as proxy/cache buckets.
:::

:::card[Cost Controls]{href="/s3-gateway/cost-controls" linkScope="title"}
Use per-key and per-key/vault gateway budgets, actual upstream accounting, synthetic local accounting, and denial diagnostics.
:::

:::card[Semantics]{href="/s3-gateway/semantics" linkScope="title"}
Understand LIST, HEAD, GET, PUT, DELETE, ETags, multipart storage, backfill, and current S3 feature limitations.
:::

:::card[Validation]{href="/s3-gateway/validation" linkScope="title"}
Run Playwright, local smoke, remote R2/S3 smoke, and merge-ready validation for the gateway feature.
:::
:::

## Related Docs

- [S3 Gateway Administration](/admin/s3-gateway) focuses on the Admin -> S3 Gateway web console and CLI management surface.
- [Vaults Exposed Through S3 Gateway](/vaults/s3-gateway) explains the vault mapping model without duplicating the gateway guide.
- [S3 And R2 Vaults](/vaults/s3-r2-vaults) covers upstream provider storage used by Vaulthalla as a vault backend.
