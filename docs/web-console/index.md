---
title: Web Console Workflows
navTitle: Web Console
description: Map common Vaulthalla web console workflows to the equivalent CLI and understand the browser-based control surface.
order: 120
status: published
tags:
  - web
  - web-console
  - workflows
---

# Web Console Workflows

The Vaulthalla web console is the browser-based control surface for most operator tasks. It is not an arbitrary host shell. It sends authenticated WebSocket commands to the Vaulthalla daemon and exposes supported workflows through pages, forms, tables, and file actions.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Web Console Versus CLI

| Task | Web console | CLI |
| --- | --- | --- |
| View runtime health | Dashboard | `vh status` |
| Browse files | Filesystem page | `/mnt/vaulthalla` and vault commands |
| Create vaults | Vaults page | `vh vault create ...` |
| Manage S3/R2 API keys | API Keys page | `vh api-key ...` |
| Manage users and groups | Users and Groups pages | `vh user ...`, `vh group ...` |
| Manage roles | Admin Roles and Vault Roles pages | `vh role ...`, `vh vault role ...` |
| Manage shares | Shares page and filesystem share action | Web-first workflow |
| Manage price budgets | Cost Control page | `vh pricing budget ...` |
| Manage S3 Gateway | Admin -> S3 Gateway | `vh s3-gateway ...` |
| Configure operator email | Operator Email page | `vh email ...` |

Use the CLI for lifecycle commands, recovery exports, automation, and host-local troubleshooting. Use the web console for interactive administration, filesystem browsing, dashboards, shares, and policy editing.

## Dashboard

The Dashboard area summarizes runtime health, filesystem activity, storage, operations, and trends. It can show setup advisories, such as an unbound CLI admin UID, without marking the whole runtime unhealthy.

Treat dashboard backup/recovery indicators as status signals. They do not prove a real backup has completed.

## Filesystem

The Filesystem page lets authenticated users browse and act on vault files according to their vault roles. Available actions depend on permissions and context.

Typical actions include:

- Preview.
- Download.
- Upload.
- Copy.
- Delete.
- Share.

If an action is missing or denied, check the user's vault role and any path overrides.

## Vault Management

The Vaults page supports local and S3/R2 vault creation. For S3/R2 vaults, the form includes:

- API key.
- Bucket.
- Storage tier.
- Sync strategy.
- Conflict policy.
- Sync interval.
- Upstream encryption.
- Request-budget preset or custom limits.
- Maximum remote-index age.

After creating or editing an S3/R2 vault, confirm the policy from the CLI:

```bash
vh vault sync info <vault>
```

## Cost Control

The Cost Control page manages price budgets. Request budgets remain part of each S3/R2 vault sync policy.

Use the CLI when you need scriptable checks:

```bash
vh pricing budget status
vh pricing budget ledger --limit 100
vh vault sync dry-run <vault>
```

## S3 Gateway

Admin -> S3 Gateway manages the downstream S3-compatible protocol surface. Use it to check service readiness, create gateway credentials, bind gateway buckets, set gateway key and key/vault budgets, review ledger/status details, and copy client snippets.

See [S3 Gateway](/s3-gateway) for endpoint setup, downstream client examples, credential scopes, bucket modes, budget behavior, and S3 operation semantics.

## Shares

The web console is the primary share workflow. It creates the one-time public URL or email-validated share URL, lets operators rotate links, and restricts share-mode file actions to the share role.

See [Sharing](/sharing).

## Operator Email

The Operator Email page configures notification providers and checks delivery history. Use it with CLI smoke tests:

```bash
vh email doctor
vh email test --dry-run
vh email history --limit 100
```

## When To Use The CLI Instead

Use `vh` instead of the web console for:

- Package lifecycle setup and teardown.
- Admin Linux UID assignment.
- Database bootstrap and remote database setup.
- Nginx and Certbot setup.
- Vault key and internal secret exports.
- Shell-based automation.
- Recovery and low-level service troubleshooting.
