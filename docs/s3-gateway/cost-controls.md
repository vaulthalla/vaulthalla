---
title: S3 Gateway Cost Controls
description: Understand S3 Gateway key budgets, key/vault budgets, actual upstream usage, synthetic local usage, and S3-compatible budget denials.
order: 50
status: published
tags:
  - s3-gateway
  - cost-control
  - budgets
  - operator
---

# S3 Gateway Cost Controls

Gateway cost controls extend Vaulthalla's S3/R2 price-budget model with budgets tied to inbound gateway credentials. They are separate from the upstream request budgets and provider/vault/global price budgets used by sync.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Gateway Budget Scopes

| Scope | Purpose |
| --- | --- |
| `gateway_credential` | Monthly cap for one inbound gateway key across all gateway buckets. |
| `gateway_credential_vault` | Monthly cap for one inbound gateway key on one vault. |

Modes match normal price budgets:

| Mode | Behavior |
| --- | --- |
| `off` | Ignore the policy. |
| `report` | Record/report usage but never block. |
| `warn` | Allow and emit warning or notification context. |
| `enforce` | Return S3 XML `AccessDenied` with HTTP 403 when the monthly cap would be exceeded. |

## Actual Upstream Usage

Gateway accounting is based on actual upstream use, not route intent. A remote-backed GET might be served from a local encrypted file, a materialized cache file, metadata, or a real upstream download. Only real upstream provider calls consume provider, vault, or global upstream budgets.

Local gateway buckets, metadata-only requests, local/cache hits, and local-first PUT/DELETE/COPY/multipart writes do not consume upstream provider budgets at gateway request time. For remote-backed writes and deletes, sync later owns upstream provider side effects and provider/vault/global ledger entries.

## Synthetic Local Usage

Gateway credentials can opt into local/cache accounting:

```bash
vh s3-gateway creds create backup --enforce-budget-for-local-requests
vh s3-gateway creds scope backup set --enforce-budget-for-local-requests
vh s3-gateway creds scope backup set --no-enforce-budget-for-local-requests
```

When enabled, pure local buckets, remote-cache hits, metadata-only requests, and sync-deferred local-first writes/deletes are recorded as synthetic usage for `gateway_credential` and `gateway_credential_vault` budgets only. Synthetic ledger rows include `synthetic=true` and a source such as `local_cache`, `local_file`, `metadata`, or `sync_deferred`.

Synthetic local usage uses `s3_gateway.synthetic_local_request_cost_usd` and never creates provider, vault, or global upstream ledger rows.

## CLI Examples

```bash
# Monthly spend cap per key. Key-wide caps are admin-managed.
vh s3-gateway budget set-key backup \
  --monthly 5 \
  --mode enforce \
  --currency USD

# Monthly spend cap for one key/vault pair.
vh s3-gateway budget set-key-vault backup \
  --vault archive \
  --monthly 2 \
  --mode enforce \
  --currency USD

vh s3-gateway budget status --key backup
vh s3-gateway budget ledger --key backup --limit 50
```

Disable policies:

```bash
vh s3-gateway budget disable-key backup
vh s3-gateway budget disable-key-vault backup --vault archive
```

## Denial Behavior

Price-budget enforcement returns S3 XML with:

| Budget failure | S3 code | HTTP status |
| --- | --- | --- |
| Gateway or upstream price budget exceeded | `AccessDenied` | 403 |
| Upstream S3/R2 request budget exceeded | `SlowDown` | 503-style S3 throttling response |

Price-budget denial messages begin with `S3 gateway price budget exceeded` and include the blocking scope, policy id, window, limit, used-before amount, remaining-before amount, requested cost, currency, provider key, vault id, gateway credential id, operation, and request UUID.

Request-budget denials begin with `S3 gateway request budget exceeded` and identify the exhausted request kind, such as `GET`, `PUT`, `DELETE`, `COPY`, or downloaded bytes.

Use budget status and ledger output to distinguish actual upstream use from synthetic local usage:

```bash
vh s3-gateway budget status --key backup --vault archive
vh s3-gateway budget ledger --key backup --limit 25
```
