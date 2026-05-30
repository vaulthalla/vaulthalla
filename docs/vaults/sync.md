---
title: Sync
description: Configure and troubleshoot local, S3, and R2 vault sync behavior, remote indexes, dry-runs, inventory imports, event ingestion, and reconcile.
order: 230
status: published
tags:
  - vaults
  - sync
  - s3
  - r2
---

# Sync

Vault sync keeps Vaulthalla's local state, metadata, and remote object storage aligned according to the vault type and sync policy. S3/R2 sync adds remote-index management, request budgeting, and price-budget checks.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Manual Sync

Trigger a sync:

```bash
vh vault sync <vault>
```

Inspect policy and remote-index state:

```bash
vh vault sync info <vault>
```

Change common policy fields:

```bash
vh vault sync set <vault> --interval 15m
vh vault sync set <vault> --on-sync-conflict ask
```

S3/R2 policy updates:

```bash
vh vault sync set archive --sync-strategy cache --on-sync-conflict keep_local
vh vault sync set archive --max-remote-index-age 24h
```

## Strategies For S3/R2

| Strategy | Use when | Important behavior |
| --- | --- | --- |
| `cache` | You want a local working cache over a bucket | Remote objects can be indexed without downloading every body. Bodies are fetched when accessed or planned. |
| `sync` | Local and remote should both accept changes | Changes on either side can be propagated according to conflict policy. |
| `mirror` | The local vault should publish to the bucket | Remote changes are not treated as local source changes. |

Choose the strategy before connecting Vaulthalla to a bucket with existing data. Use dry-run and request budgets to understand planned work.

## Conflict Policies

Local vault policies:

- `overwrite`
- `keep_both`
- `ask`

S3/R2 vault policies:

- `keep_local`
- `keep_remote`
- `ask`

Use `ask` when automatic conflict resolution would be risky and an operator should review conflicts.

## Remote Index

S3/R2 vaults maintain a remote object index. The index lets Vaulthalla plan sync work without repeatedly scanning a large bucket.

The remote-index flow can use:

- A Vaulthalla manifest under `.vaulthalla/`.
- Local database index rows.
- S3 Inventory imports.
- S3 Event Notification ingestion.
- Explicit reconcile scans.

`vh vault sync info <vault>` reports remote-index source, indexed time, manifest ETag, generated time, object count, and fresh or stale status.

If the local index is older than `max_remote_index_age` and the manifest cannot be refreshed, sync stalls instead of silently trusting stale data.

## Dry-Run

Run dry-run before raising budgets or syncing a large bucket:

```bash
vh vault sync dry-run <vault>
```

Default dry-run is local-index-only. It reads local state and the local remote index, builds a plan, and prints estimated request and traffic pressure. It does not invent a plan when no usable remote index exists.

Refresh the remote index only when you intentionally allow S3 work:

```bash
vh vault sync dry-run <vault> --refresh-index
vh vault sync dry-run <vault> --refresh-remote-index
```

Pricing controls:

```bash
vh vault sync dry-run <vault> --refresh-pricing
vh vault sync dry-run <vault> --no-pricing
```

Dry-run output is the safest way to compare request budgets, price budgets, and planned body downloads before real sync work starts.

## Import S3 Inventory

For large existing buckets, import S3 Inventory instead of starting with a full bucket scan:

```bash
vh vault sync inventory <vault> --file inventory.csv
```

For CSVs without headers, provide a schema:

```bash
vh vault sync inventory <vault> \
  --file inventory.csv \
  --schema bucket,key,size,last_modified_date,etag,storage_class
```

Inventory import indexes object metadata and publishes the Vaulthalla manifest without downloading object bodies.

## Ingest S3 Events

Use event ingestion to keep the index current after initial import:

```bash
vh vault sync events <vault> --file s3-events.json
```

ObjectCreated events upsert remote-index rows. ObjectRemoved events delete rows. Sequencers prevent older events from overwriting newer index state when the provider supplies them. Manifest objects under `.vaulthalla/` are ignored.

## Reconcile

Reconcile performs an explicit ListObjectsV2 pass:

```bash
vh vault sync reconcile <vault> --allow-list-scan
```

Without `--allow-list-scan`, reconcile requires a configured LIST request budget on the sync policy. This prevents accidental unbounded scans on large buckets.

When a prior index exists, reconcile prints a rough estimate of one LIST request per 1,000 indexed objects. Use that estimate to set an appropriate `--s3-budget-list` value.

## Archive-Tier Objects

Archive-tier objects can be indexed without body download. If the provider requires restore before download, Vaulthalla skips planned body downloads and reports the skip in sync or dry-run output. Restore the object with the provider first, then run sync again.

## Budget Stalls

When a request or downloaded-byte budget is exceeded, the sync event is marked `stalled` with a budget-related reason. This is expected protective behavior, not necessarily a service failure.

Start with:

```bash
vh vault sync info <vault>
vh vault sync dry-run <vault>
```

Then raise the narrow exhausted budget field rather than switching to unlimited.
