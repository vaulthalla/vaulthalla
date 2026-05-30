---
title: Price Budgets
description: Configure global, provider, and vault-level price budgets for S3/R2 operations.
order: 320
status: published
tags:
  - cost-control
  - pricing
  - budgets
---

# Price Budgets

Price budgets estimate provider cost before remote work runs. They can report, warn, or enforce global, provider-level, and vault-level limits.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Policy Levels

| Level | Scope | Example |
| --- | --- | --- |
| Global | All priced storage operations | Cap all S3/R2 sync work to a daily limit. |
| Provider | One provider such as AWS S3 or Cloudflare R2 | Enforce tighter rules on AWS than R2. |
| Vault | One vault | Give a large archive vault its own run limit. |

Supported provider keys include:

- `aws-s3`
- `cloudflare-r2`

## Modes

| Mode | Behavior |
| --- | --- |
| `off` | Policy is disabled. |
| `report` | Estimate and record cost without warning or blocking. |
| `warn` | Warn when limits are exceeded. |
| `enforce` | Block work that exceeds policy unless an approved override is consumed. |

Start with `report`, move to `warn`, then move to `enforce` after the estimates match your expectations.

## Limits

Common limits:

- `--max-run`: maximum estimated cost for one operation or sync run.
- `--max-daily`: maximum estimated cost per day.
- `--max-monthly`: maximum estimated cost per month.
- `--currency`: currency code, usually `USD`.

Examples:

```bash
vh pricing budget set-global --mode warn --max-daily 5 --currency USD
vh pricing budget set-provider aws-s3 --mode enforce --max-run 1 --max-daily 10
vh pricing budget set-vault archive --mode report --max-run 0.25
```

## Catalog Freshness

Price estimates depend on pricing catalog data. Policy options can control catalog freshness:

```bash
vh pricing budget set-global \
  --mode warn \
  --max-daily 5 \
  --allow-stale-catalog \
  --max-catalog-age 43200
```

Use stricter catalog verification for enforcement policies when stale pricing would be unacceptable.

## Status And Ledger

List policies:

```bash
vh pricing budget list
```

Check current status:

```bash
vh pricing budget status
```

Read recent ledger entries:

```bash
vh pricing budget ledger --limit 100
```

Disable policies:

```bash
vh pricing budget disable-global
vh pricing budget disable-provider aws-s3
vh pricing budget disable-vault archive
```

## Web Console

Use the Cost Control page for interactive policy management. It exposes policy editors, status, ledger views, notifications, and override handling.

## How Price Budgets Interact With Request Budgets

Request budgets and price budgets evaluate different risks:

- Request budgets cap operation counts and downloaded bytes.
- Price budgets cap estimated currency exposure.

A sync can pass one and fail the other. For example, a run might have few requests but high download cost, or many cheap requests that exceed the LIST budget. Use both for production S3/R2 vaults.

## Enforcement Stalls

When price enforcement blocks work, the sync event should stop before remote work proceeds. Inspect:

```bash
vh pricing budget status
vh pricing budget ledger --limit 50
vh vault sync dry-run <vault>
```

If the run is intentional, raise the appropriate policy or approve an override through the operator flow.
