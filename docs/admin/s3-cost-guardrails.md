# S3 Cost Guardrails Runbook

Vaulthalla S3 vaults apply a non-unlimited request budget by default. New S3
sync policies start with the `balanced` preset unless an operator changes the
policy with `vh vault sync set`.

## Budget Presets

Use presets for predictable defaults, then override individual fields only when
the workload needs it:

```sh
vh vault sync set <vault> --s3-budget-preset conservative
vh vault sync set <vault> --s3-budget-preset balanced
vh vault sync set <vault> --s3-budget-preset bulk
vh vault sync set <vault> --s3-budget-preset unlimited
```

Individual flags override the selected preset:

```sh
vh vault sync set <vault> --s3-budget-preset conservative --s3-budget-get 25
```

Recommended starting points:

- `conservative`: small buckets, exploratory imports, or operators validating
  a new policy.
- `balanced`: default for normal small-to-medium buckets.
- `bulk`: planned high-volume imports or maintenance windows.
- `unlimited`: only for controlled one-off operations where S3 cost is already
  bounded externally.

## Small-Bucket Setup

1. Create the S3 vault normally. The `balanced` budget is applied to new S3
   sync policies by default.
2. Run `vh vault sync info <vault>` and confirm the S3 request budget.
3. Trigger a normal sync with `vh vault sync <vault>`.
4. If the event stalls with a budget reason, raise only the exhausted budget
   field instead of switching to `unlimited`.
5. Leave `max_remote_index_age` at its default unless the bucket is updated
   exclusively through Vaulthalla or event ingestion is known to be reliable.

## Large-Bucket Setup

Avoid starting with a full ListObjectsV2 scan on large buckets.

1. Configure `conservative` or a custom LIST budget before the first run.
2. Import S3 Inventory first when available.
3. Enable event ingestion to keep the remote index warm.
4. Use `vh vault sync dry-run <vault>` to inspect planned request pressure.
5. Run reconcile only during a maintenance window with either an explicit LIST
   budget or `--allow-list-scan`.
6. Set a remote-index freshness window that matches the event/inventory cadence:

```sh
vh vault sync set <vault> --max-remote-index-age 24h
```

## Import S3 Inventory First

Use S3 Inventory for the first remote index when the bucket may contain many
objects:

```sh
vh vault sync inventory <vault> --file inventory.csv
```

For CSVs without a header:

```sh
vh vault sync inventory <vault> --file inventory.csv --schema bucket,key,size,last_modified_date,etag,storage_class
```

Inventory import indexes object metadata and publishes the Vaulthalla manifest
without downloading object bodies.

## Event-Ingestion Path

Use S3 event notifications to keep the index current after the initial import:

```sh
vh vault sync events <vault> --file s3-events.json
```

ObjectCreated events upsert index rows. ObjectRemoved events delete index rows.
When S3 provides object sequencers, older events do not overwrite newer index
state. Manifest objects under `.vaulthalla/` are ignored.

`vh vault sync info <vault>` reports the remote index source, indexed time,
manifest ETag, manifest generated time, object count, and fresh/stale status.
If the local index is older than `max_remote_index_age` and the manifest cannot
be refreshed, sync stalls instead of silently trusting stale data.

## Reconcile

`vh vault sync reconcile` performs an explicit ListObjectsV2 pass. It requires
one of:

- a configured `--s3-budget-list` value on the sync policy, or
- the explicit `--allow-list-scan` flag.

When a prior index exists, the command prints a rough pre-run estimate of one
LIST request per 1,000 indexed objects. Use this estimate to choose a LIST
budget before running reconcile on a large bucket.

## Dry Run

Use dry-run before changing a budget or launching a large sync:

```sh
vh vault sync dry-run <vault>
```

The command may refresh the remote index manifest before planning. It builds the
next sync plan from the local files and remote index, and prints estimated LIST,
HEAD, GET, PUT, COPY, DELETE, body-download bytes, upload bytes,
cache/index-only objects, and archive-tier body downloads skipped. It refuses to
invent a plan when no remote index exists; import Inventory, ingest events, or
reconcile first.

## Failure Modes

When a budget is exceeded, the sync event is marked `stalled` and
`stall_reason` contains the budget reason. Budget exhaustion should not appear
as a generic sync `error` unless another exception occurs after the budget
failure.

Downloaded-byte budgets are checked before planned body downloads and during
body transfer callbacks. Cache remote-only indexing is tracked as index work and
does not count as downloaded traffic.

Manifest publishes use conditional S3 PUTs when an ETag is known. If another
writer wins the race, Vaulthalla reloads the latest manifest, replays the local
index mutations, and retries a bounded number of times. Repeated conflicts stall
the sync with a manifest conflict reason instead of silently losing updates.
