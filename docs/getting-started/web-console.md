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
```

### HTTP And HTTPS

A fresh package install serves the console over plain HTTP on port 80 (`http://<host>/`), which is enough to finish first run on a trusted network. The session cookie is marked `Secure` only when the browser-facing request arrived over HTTPS, as reported by the local Nginx proxy (`X-Forwarded-Proto`), so login works on both.

:::callout{theme="warning" title="Use HTTPS beyond a trusted LAN"}
Over plain HTTP, passwords and session tokens cross the network unencrypted. Before exposing the console, enable TLS with `sudo vh setup nginx --domain vault.example.com --certbot`.
:::

### First Login

There is no default password. When the service first starts with a new database, it generates a strong password for the web `admin` account (16 random bytes, shown as 32 hex characters), unique to this install, and writes a copy to a root-readable file:

```bash
sudo cat /var/lib/vaulthalla/super_admin_initial_password
```

Sign in as `admin` with it. The session is a normal one; nothing forces you to change the password first. Repeated failed logins are rate-limited per client address and account.

Changing it is recommended but not required:

- Change it with `vh setup set-super-admin-password`, run as the Linux user bound as the Vaulthalla super admin (see `vh setup assign-admin`), without `sudo`. Changing it from the web console works too. Either way the file is removed.
- Or keep the generated password and delete the file: `sudo rm /var/lib/vaulthalla/super_admin_initial_password`. Deleting the file doesn't change the password.

Until you do one or the other, the console shows the super admin a warning, and `vh setup nginx` asks what to do before putting the console behind nginx. The file is written only once: deleting it never makes a new one appear, and upgrades, reinstalls and restarts keep the existing password.

Installs older than 1.8.0 seeded the same default password everywhere. If `admin` still uses it, the first start of 1.8.0 replaces it with a generated one, writes it to the file above, and ends the account's web sessions. Passwords you set yourself are not touched.

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

## S3 Gateway Page

Admin -> S3 Gateway manages downstream S3-compatible access. Credential creation chooses the effective principal from a relational user selector when the actor has `admin.s3_gateway.assign_principal`; users without that permission see their own principal only.

Gateway authorization is RBAC-native. `user_access` credentials inherit the principal user's Vaulthalla RBAC without gateway role rows. `vault_allowlist` credentials are managed through selected vaults, default vault roles, role exceptions, and path overrides in the credential role editor. Boolean scope flags are CLI/API shorthand only and are not the web-console policy model.

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
