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

Deactivate or reactivate an account:

```bash
vh user update alice --disable
vh user update alice --enable
```

Delete:

```bash
vh user delete alice                      # asks first; her vaults are destroyed
vh user delete alice --transfer-to bob    # asks first; her vaults go to bob
vh user delete alice --yes                # no question (scripts)
```

Deleting an account always asks: "Are you sure you wish to delete this user? The user's existing vaults will be destroyed unless ownership is transferred." The web console asks the same in its delete dialog, where you can pick who receives the vaults. Every vault is checked before anything changes, so a refused transfer or removal leaves the account and its vaults as they were.

Transferring vault ownership, here or with `vh vault update <id> --owner <user>` / the vault edit page, is limited to administrators: it needs an admin account, edit rights on the vault, and the right to create vaults for the new owner. The new owner can't already have a vault with the same name.

The built-in super admin user and role are protected from ordinary mutation paths. The super admin can't be renamed, because the daemon looks it up by name. Its web console password starts as a generated, per-install one; see [Web Console](/getting-started/web-console#first-login). Change it with `vh setup set-super-admin-password`, run as the Linux user bound as the super admin.

Rules that apply on the CLI and in the web console alike:

- An account whose admin role grants anything beyond managing its owner's own vaults and keys is an *admin account*. Creating, editing, deleting or resetting the password of an admin account needs the `admin.identities.admins.*` permissions; plain accounts use `admin.identities.users.*`.
- Nobody can assign a role that grants admin permissions they don't hold. Nobody can edit, delete, deactivate or reset the password of an account whose role exceeds their own.
- Nobody can change their own role, Linux UID binding or active state, or delete their own account. Ask another administrator.
- An account's global vault policy (what it may do in vaults through no specific vault role) is seeded from its role when the role is assigned (the built-in role's preset), then belongs to the account. `unprivileged` grants none, and custom roles seed `unprivileged`: such accounts reach a vault, their own default vault included, through a vault role assignment.
- Deleting, deactivating, re-roling or resetting the password of an account ends all of its sessions immediately. A deactivated account can't log in.
- `vh user create` creates the account's default vault, as the web console does, and prints a generated password once.

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

To start from an existing role, pass `--from` and then the flags that differ:

```bash
vh role admin create support-lead --from support --allow-<permission> ...
```

`--from` copies that role's permissions into the new one as a starting point. Without it, a role starts from `unprivileged`. Once created, the role owns its own permissions: later changes to the role it came from don't reach it. The web console's role form does the same with its **Start from** list.

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

The web console groups these under **Access**: Users, Groups, and Roles (admin and vault roles in one list, with a grouped permission editor). Use them for interactive administration and use `vh` for scriptable or recovery-oriented operations.

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
