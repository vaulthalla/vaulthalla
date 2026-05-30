---
title: Cost Control
description: Understand Vaulthalla's two cost-control layers for S3 and R2 vaults: request budgets and price budgets.
order: 300
status: published
tags:
  - cost-control
  - s3
  - r2
---

# Cost Control

Vaulthalla has two complementary cost-control systems for S3-compatible vaults:

1. Request budgets on vault sync policies.
2. Price budgets using provider pricing estimates and policy enforcement.

Use both for important S3/R2 vaults. Request budgets bound the shape of a sync run, while price budgets bound estimated currency exposure.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Which Control Should I Use?

| Goal | Use |
| --- | --- |
| Prevent an accidental bucket-wide LIST scan | Request budgets |
| Cap GET, PUT, DELETE, COPY, HEAD, or download-byte pressure | Request budgets |
| Enforce a max estimated cost per run, day, or month | Price budgets |
| Warn before S3/R2 work becomes expensive | Price budgets |
| Run large planned maintenance with explicit limits | Both |
| Keep a large bucket indexed without repeated scans | Request budgets plus inventory/events |

## Request Budgets

Request budgets live on an S3/R2 vault sync policy. They cap planned and observed S3 operations such as LIST, HEAD, GET, PUT, COPY, DELETE, and downloaded bytes.

Configure them with:

```bash
vh vault sync set <vault> --s3-budget-preset balanced
vh vault sync set <vault> --s3-budget-get 1000 --s3-budget-list 100
```

See [Request Budgets](/cost-control/request-budgets) and the detailed [S3 Cost Guardrails Runbook](/admin/s3-cost-guardrails).

## Price Budgets

Price budgets estimate provider costs and apply policies at global, provider, or vault level.

Configure them with:

```bash
vh pricing budget set-global --mode warn --max-daily 5 --currency USD
vh pricing budget set-provider cloudflare-r2 --mode enforce --max-run 1
vh pricing budget set-vault <vault> --mode report --max-run 0.25
```

See [Price Budgets](/cost-control/price-budgets).

## Web Console

The web console exposes both cost-control concepts in different places:

- Vault form and sync policy controls: request-budget preset, custom request limits, sync interval, max remote-index age, strategy, and conflict policy.
- Cost Control page: price-budget policies, status, ledger, notifications, and overrides.

## Recommended Policy

For a new S3/R2 vault:

1. Start with upstream encryption enabled.
2. Use `cache` strategy unless you know you need full two-way sync.
3. Keep the default balanced request budget or choose conservative for unknown buckets.
4. Set a vault-level price budget in `report` or `warn` mode.
5. Run `vh vault sync dry-run <vault>` before the first real sync.
6. Import S3 Inventory before scanning a large existing bucket.
7. Move price policy to `enforce` only after the estimates match your provider billing model.

## What A Stall Means

When a cost control blocks work, Vaulthalla usually records the sync event as stalled with a reason. A stall is a safety stop. Inspect the reason, dry-run the next plan, and raise only the limit that matches the intended operation.
