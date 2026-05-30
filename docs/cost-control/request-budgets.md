---
title: Request Budgets
description: Configure S3 and R2 request budgets for LIST, HEAD, GET, PUT, COPY, DELETE, and downloaded bytes.
order: 310
status: published
tags:
  - cost-control
  - request-budgets
  - s3
  - r2
---

# Request Budgets

Request budgets limit how much S3/R2 request pressure a vault sync policy can spend. They are the first defense against accidental scans, runaway downloads, and surprise provider bills.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Presets

Use presets first, then override individual fields only when needed:

```bash
vh vault sync set <vault> --s3-budget-preset conservative
vh vault sync set <vault> --s3-budget-preset balanced
vh vault sync set <vault> --s3-budget-preset bulk
vh vault sync set <vault> --s3-budget-preset unlimited
```

| Preset | Use for |
| --- | --- |
| `conservative` | Unknown buckets, exploratory imports, or first sync tests. |
| `balanced` | Normal small-to-medium vaults. |
| `bulk` | Planned high-volume imports or maintenance windows. |
| `unlimited` | Controlled one-off work where cost is bounded outside Vaulthalla. |

## Custom Limits

Set specific request budgets:

```bash
vh vault sync set archive \
  --s3-budget-list 100 \
  --s3-budget-head 1000 \
  --s3-budget-get 1000 \
  --s3-budget-put 1000 \
  --s3-budget-copy 100 \
  --s3-budget-delete 1000 \
  --s3-budget-download-bytes 10G
```

Clear a specific field with values such as `unlimited`, `none`, or `null`:

```bash
vh vault sync set archive --s3-budget-get unlimited
```

## Budget Fields

| Field | What it controls |
| --- | --- |
| `--s3-budget-list` | Bucket listing and reconcile scan pressure. |
| `--s3-budget-head` | Remote object metadata checks. |
| `--s3-budget-get` | Object GET requests and manifest reads. |
| `--s3-budget-put` | Uploads and manifest publishes. |
| `--s3-budget-copy` | Provider-side object copy operations. |
| `--s3-budget-delete` | Remote delete operations. |
| `--s3-budget-download-bytes` | Body download traffic. |

## Remote Index Freshness

Set the maximum age of the local remote index:

```bash
vh vault sync set archive --max-remote-index-age 24h
```

Use `unlimited` only when another process guarantees the index is correct:

```bash
vh vault sync set archive --max-remote-index-age unlimited
```

If the index is stale and cannot be refreshed, sync stalls instead of trusting potentially wrong remote state.

## Large Bucket Pattern

For large existing buckets:

1. Create the vault with `conservative` request budgets.
2. Import S3 Inventory:

```bash
vh vault sync inventory <vault> --file inventory.csv
```

3. Ingest S3 events after the inventory point:

```bash
vh vault sync events <vault> --file s3-events.json
```

4. Dry-run:

```bash
vh vault sync dry-run <vault>
```

5. Reconcile only in a maintenance window:

```bash
vh vault sync reconcile <vault> --allow-list-scan
```

## Web Console

In the S3/R2 vault form, use the Cost Controls section to choose a request-budget preset or custom limits. The same form also controls sync interval, max remote-index age, sync strategy, conflict policy, and whether sync is enabled.

After saving, verify with:

```bash
vh vault sync info <vault>
```

## Troubleshooting Budget Stalls

If sync stalls:

```bash
vh vault sync info <vault>
vh vault sync dry-run <vault>
```

Read the stall reason, then raise only the exhausted field. For example, if LIST is exhausted during reconcile, raise `--s3-budget-list` or switch to inventory/event ingestion instead of making every request type unlimited.
