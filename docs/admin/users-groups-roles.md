---
title: Users, Groups, And Roles
description: Manage Vaulthalla users, Linux UID mappings, groups, admin roles, vault roles, and vault assignments.
order: 5
status: published
tags:
  - admin
  - users
  - roles
  - permissions
---

# Users, Groups, And Roles

Vaulthalla uses application users, groups, admin roles, vault roles, and optional Linux UID/GID mappings. The CLI also depends on local Linux identity for trusted operator access.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Identity Model

| Item | Purpose |
| --- | --- |
| Linux user | Controls local host login and access to the CLI socket. |
| Application user | Vaulthalla identity used for permissions and audit records. |
| `linux_uid` | Optional mapping from Linux UID to an application user. |
| Group | Team-level permission subject. |
| Admin role | Instance-level administration permissions. |
| Vault role | Vault-level data and management permissions. |

The first CLI admin should be bound intentionally with [First Run](/getting-started/first-run).

## Users

Create a user:

```bash
vh user create alice --role admin --email alice@example.com --linux-uid 1001
```

Inspect and update:

```bash
vh user info alice
vh user update alice --email alice@new.example.com
vh user update alice --linux-uid 1001
```

Delete:

```bash
vh user delete alice
```

The built-in super admin user and role are protected from ordinary mutation paths.

## Groups

Create a group:

```bash
vh group create operators --desc "Operations team" --linux-gid 2001
```

Manage membership:

```bash
vh group user add operators alice
vh group user remove operators alice
vh group users operators
```

Use groups for vault access whenever more than one person should receive the same vault permissions.

## Admin Roles

List supported admin permissions:

```bash
vh permissions --type user
```

Create an admin role:

```bash
vh role admin create operations-admin \
  --manage-users \
  --manage-groups \
  --manage-vaults \
  --manage-api-keys \
  --audit-log-access
```

Useful admin permission areas include:

- User management.
- Group management.
- Vault management.
- Role management.
- API key management.
- Encryption key export.
- Audit log access.
- Admin management.

Grant only the permissions needed for the operator's job.

## Vault Roles

List supported vault permissions:

```bash
vh permissions --type vault
```

Create a vault role:

```bash
vh role vault create read-share \
  --list \
  --download \
  --share
```

Vault permission areas include:

- List and browse.
- Create, download, delete, rename, and move.
- Share.
- Sync.
- Tags, metadata, versions, and file locks.
- Vault access and vault management.

## Assign Vault Roles

Assign to a user:

```bash
vh vault role assign archive <role-id> --user alice
```

Assign to a group:

```bash
vh vault role assign archive <role-id> --group operators
```

List assignments:

```bash
vh vault role list archive
```

Remove an assignment:

```bash
vh vault role unassign archive <role-id> --user alice
```

## Path Overrides

Use overrides when a subject needs a different permission result for a path pattern:

```bash
vh vault role override add archive \
  --user alice \
  --pattern "/finance/*" \
  --download \
  --disable
```

List and remove:

```bash
vh vault role override list archive
vh vault role override remove archive <override-id>
```

Keep overrides rare. They are powerful but harder to audit than simple role assignments.

## Web Console

The web console includes Users, Groups, Admin Roles, and Vault Roles pages. Use them for interactive administration and use `vh` for scriptable or recovery-oriented operations.

## Troubleshooting Access

If a user cannot use the CLI:

```bash
id
getent group vaulthalla
ls -l /run/vaulthalla/cli.sock
vh user info <username>
```

If a user can log in but cannot see vault content:

```bash
vh vault role list <vault>
vh permissions --type vault
```

Check group assignments and path overrides before changing broad admin roles.
