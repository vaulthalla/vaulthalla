---
title: Web Console
description: Use the Vaulthalla browser console for dashboards, files, vaults, sharing, users, roles, API keys, cost control, and operator email.
order: 30
status: published
tags:
  - web
  - admin
  - operator
---

# Web Console

The Vaulthalla web console is a packaged Next.js application served locally by `vaulthalla-web.service` and normally exposed through the Nginx site created by `sudo vh setup nginx`.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Access

After Nginx setup, open the configured domain:

```text
https://vault.example.com
```

The packaged web runtime listens on localhost, while the core daemon exposes WebSocket and preview endpoints on local ports for the Nginx proxy. Operators normally do not connect to those ports directly.

Relevant services:

```bash
systemctl status vaulthalla-web.service
systemctl status vaulthalla.service
systemctl status vaulthalla-cli.socket
```

## Main Areas

The web console includes:

- Dashboard pages for runtime health, filesystem activity, storage, operations, and trends.
- Filesystem browser for browsing `/mnt/vaulthalla` through authenticated application permissions.
- Vault management for local and S3-compatible vaults.
- Share management for public or email-validated links.
- Cost Control for pricing policies, request budgeting context, overrides, ledger, and status.
- Operator Email for notification provider setup and diagnostics.
- API Keys for S3-compatible providers.
- Users, Groups, Admin Roles, and Vault Roles for access control.
- Settings for instance-level configuration.

## Filesystem Browser

Use the filesystem browser for common file operations through the web UI. The available actions depend on the current path, the selected item, and the user's role permissions. Typical actions include browsing, previewing, downloading, copying, deleting, sharing, and uploading where permitted.

Share-mode browsing has a smaller action set. A public or email-validated share only grants the operations included in that share role, such as metadata, list, preview, download, upload, or mkdir.

## Vault Form

Create local and S3/R2 vaults from the Vaults area.

For local vaults, choose the vault name, description, quota, and sync conflict behavior.

For S3-compatible vaults, choose:

- API key.
- Bucket.
- Storage tier or storage class.
- Sync strategy: `cache`, `sync`, or `mirror`.
- Conflict policy.
- Sync interval.
- Upstream encryption.
- Request budget preset or custom request limits.
- Maximum remote index age.

The web form defaults new S3 vaults toward bounded behavior: cache-style sync, upstream encryption enabled, balanced request budgeting, and a finite remote-index freshness window.

## Cost Control Page

The Cost Control page manages price budgets. Use it to set global, provider-level, or vault-level policies; review status and ledger entries; and handle approved overrides. Request budgets for a specific S3/R2 vault are configured on the vault sync policy and are covered in [Request Budgets](/cost-control/request-budgets).

## Operator Email Page

Use Operator Email to configure provider credentials, run dry-run and send tests, inspect delivery history, and confirm notification routing. See [Operator Emails](/admin/operator-emails).

## Troubleshooting Access

If the web console is unavailable:

```bash
systemctl status vaulthalla-web.service
journalctl -fu vaulthalla-web.service
systemctl status vaulthalla.service
journalctl -fu vaulthalla.service
sudo nginx -t
```

If login succeeds but data views fail, check the core daemon logs and WebSocket proxy path first. If previews or downloads fail, also check the preview endpoint proxy and vault permissions.
