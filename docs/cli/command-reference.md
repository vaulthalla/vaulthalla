---
title: Command Reference
description: Operator-oriented reference for Vaulthalla CLI command families and common examples.
order: 110
status: published
tags:
  - cli
  - reference
---

# Command Reference

This is an operator reference for the command families exposed through `vh`. Use `vh help <namespace>` on the host for the exact help text shipped by the installed version.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Global

| Command | Purpose |
| --- | --- |
| `vh help` | Show root help. |
| `vh help <namespace>` | Show help for a namespace or subcommand. |
| `vh version` | Print the installed CLI version. |
| `vh status` | Print runtime/service status. |

## Setup And Teardown

```bash
vh setup assign-admin
sudo vh setup db
sudo vh setup remote-db --host <host> --port 5432 --user <user> --database <name> --password-file <path>
sudo vh setup nginx --domain vault.example.com
sudo vh setup nginx --domain vault.example.com --certbot
sudo vh setup nginx --domain vaulthalla.dev --s3-domain s3.vaulthalla.dev --certbot-dns-cloudflare --cloudflare-credentials /etc/vaulthalla/certbot/cloudflare.ini
sudo vh teardown nginx
sudo vh teardown db
```

`setup assign-admin` is a normal CLI command. The database, remote database, Nginx, and teardown commands are privileged lifecycle commands and should be run with `sudo`.

## Users

Aliases include `vh users`, `vh user`, and `vh u`.

```bash
vh user create <username> --role <role-or-id> [--email <email>] [--linux-uid <uid>]
vh user info <username-or-id>
vh user update <username-or-id> --name <new-name> --email <email> --role <role-or-id> --linux-uid <uid>
vh user delete <username-or-id>
```

The built-in `super_admin` role and user are protected from normal create, update, and delete operations.

## Groups

```bash
vh group create <name> [--desc <description>] [--linux-gid <gid>]
vh group info <name-or-id>
vh group update <name-or-id> --name <new-name> --desc <description> --linux-gid <gid>
vh group delete <name-or-id>
vh group user add <group> <user>
vh group user remove <group> <user>
vh group users <group>
```

Use groups when permissions should follow a team rather than an individual user.

## Roles And Permissions

List supported permissions:

```bash
vh permissions
vh permissions --type user
vh permissions --type vault
```

Admin roles:

```bash
vh role admin list
vh role admin info <role>
vh role admin create <name> --manage-users --manage-vaults
vh role admin update <role> --audit-log-access
vh role admin delete <role>
```

Vault roles:

```bash
vh role vault list
vh role vault info <role>
vh role vault create <name> --list --download --sync
vh role vault update <role> --share
vh role vault delete <role>
```

Admin permissions include user, group, role, vault, API key, encryption key, audit, and admin management capabilities. Vault permissions include list, create, download, delete, rename, move, share, sync, version, tag, metadata, file lock, access, and vault management capabilities.

## API Keys

Aliases include `vh api-key`, `vh aku`, and `vh ak`.

```bash
vh api-key list
vh api-key create <name> \
  --access <access-key> \
  --secret <secret-key> \
  --provider <provider> \
  --endpoint <url> \
  [--region <region>]
vh api-key info <name-or-id>
vh api-key delete <name-or-id>
```

Supported provider values include `aws`, `cloudflare-r2`, `wasabi`, `backblaze-b2`, `digitalocean`, `minio`, `ceph`, `storj`, and `other`.

Cloudflare R2 example:

```bash
vh api-key create r2-main \
  --access <access-key> \
  --secret <secret-key> \
  --provider cloudflare-r2 \
  --endpoint https://<account-id>.r2.cloudflarestorage.com
```

The endpoint is required. The default region is `auto`.

## Vaults

```bash
vh vaults
vh vaults --local
vh vaults --s3 --limit 5
vh vaults --json
vh vault info <id-or-name> [--owner <user-or-id>]
vh vault delete <id-or-name> [--owner <user-or-id>]
```

Create a local vault:

```bash
vh vault create docs --local --desc "Team documents" --quota 50G --on-sync-conflict keep_both
```

Create an S3/R2 vault:

```bash
vh vault create archive \
  --s3 \
  --api-key r2-main \
  --bucket vaulthalla-archive \
  --sync-strategy cache \
  --on-sync-conflict keep_local \
  --encrypt
```

Update a vault:

```bash
vh vault update archive --sync-strategy sync --interval 15m
```

## Vault Access

Assign a vault role to a user or group:

```bash
vh vault role assign <vault> <role-id> --user alice
vh vault role assign <vault> <role-id> --group operators
```

Remove a vault role:

```bash
vh vault role unassign <vault> <role-id> --user alice
vh vault role list <vault>
```

Add permission overrides for a path pattern:

```bash
vh vault role override add <vault> --user alice --pattern "/finance/*" --download --disable
vh vault role override list <vault>
vh vault role override remove <vault> <override-id>
```

## Sync

```bash
vh vault sync <vault>
vh vault sync info <vault>
vh vault sync set <vault> --interval 15m
vh vault sync dry-run <vault>
vh vault sync inventory <vault> --file inventory.csv
vh vault sync events <vault> --file s3-events.json
vh vault sync reconcile <vault> --allow-list-scan
```

S3/R2 sync policy fields include strategy, conflict policy, interval, request budgets, and maximum remote-index age. See [Sync](/vaults/sync) and [Request Budgets](/cost-control/request-budgets).

## Vault Keys

```bash
vh vault keys export <vault-or-all> --recipient <gpg-fingerprint> --output vaulthalla-vault-keys.json.gpg
vh vault keys export <vault-or-all> --output vaulthalla-vault-keys.json
vh vault keys rotate <vault-or-all> [--sync-now]
```

Unencrypted key exports are dangerous. Prefer `--recipient` and `--output`.

## Internal Secrets

```bash
vh secret set db-password /root/db-password
vh secret set jwt-secret /root/jwt-secret
vh secret export db-password --recipient <gpg-fingerprint> --output db-password.json.gpg
vh secret export jwt-secret --recipient <gpg-fingerprint> --output jwt-secret.json.gpg
vh secret export all --recipient <gpg-fingerprint> --output vaulthalla-secrets.json.gpg
```

`secret set` reads the secret value from the file path you pass.

## Pricing Budgets

```bash
vh pricing budget list
vh pricing budget set-global --mode warn --max-daily 5 --currency USD
vh pricing budget set-provider aws-s3 --mode enforce --max-run 1 --max-daily 10
vh pricing budget set-vault <vault> --mode report --max-run 0.25
vh pricing budget status
vh pricing budget ledger --limit 100
vh pricing budget disable-vault <vault>
```

See [Price Budgets](/cost-control/price-budgets).

## Email

```bash
vh email provider resend set
vh email provider ses set
vh email doctor
vh email test --dry-run
vh email test --send --to ops@example.com
vh email history --limit 100
```

See [Operator Emails](/admin/operator-emails).
