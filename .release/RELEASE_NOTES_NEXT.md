# One rulebook for the CLI and the web console

The CLI and the web console now share one implementation of every account, role, group, vault, API key, S3
gateway, budget and settings operation, so both apply the same permission rules. The audit that started this
found eleven places where the two disagreed; all are closed.

## Before you upgrade

- **The universal admin password is gone.** If the web `admin` account still uses the old default password,
  the first start of 1.8.0 replaces it with a random one, ends admin's web sessions and writes the new password
  to `/var/lib/vaulthalla/super_admin_initial_password`. Read it with
  `sudo cat /var/lib/vaulthalla/super_admin_initial_password`. Passwords you set yourself are not touched.
- New installs get a random admin password in the same file. Change it with `vh setup set-super-admin-password`
  (run as the Linux user bound as the super admin, without `sudo`), or keep it and delete the file. Nothing
  forces a password change at sign-in any more.
- Deleting a user now asks first, and their vaults are destroyed unless you transfer them:
  `vh user delete <user> --transfer-to <other>`. Scripts need `--yes`.

## Security

- Admin accounts are judged by what their role actually grants. Nobody can assign a role above their own, or
  manage an account whose role exceeds theirs; the super admin can't be renamed or have its password reset by
  others.
- Deleting, deactivating, re-roling or resetting a user ends that user's sessions immediately, and deactivated
  users can't sign in.
- List commands refuse SQL injected through `--sort`.
- S3 gateway role grants can't exceed the person granting them, bucket names aren't revealed before
  authorization, and a vault name never resolves to another owner's vault.
- Vault budgets need vault edit rights, so an owner can't drop a budget an administrator set. Only administrators
  can transfer vault ownership.

## Fixes

- Accounts on built-in roles can use their vaults through their role's global policy again. Every account had
  been written with an empty one, which also left the S3 gateway bucket list empty. Startup repairs existing
  accounts.
- Vault changes take effect without a restart, and `vh status` and the web console agree when PostgreSQL is down.
- Login rate limiting sees real client addresses behind nginx (#125), and login bursts no longer stall other
  connections (#132). Intermittent nginx 502s during the periodic session sweep are fixed.
- Stopping the S3 gateway or preview service no longer risks a crash while connections are open.
- Role permission flags, `vh role admin create --from` and vault role overrides work as documented, and unknown
  CLI options are rejected instead of ignored.

## New

- The web console's delete-user dialog asks the same question as the CLI and can transfer the user's vaults; the
  vault form picks a new owner by username.
- New roles can start from an existing one (`--from`, or "Start from" in the web console) and then keep their own
  copy of its permissions.
- `vh setup nginx` warns before exposing the console while the generated admin password's plaintext file still
  exists, and offers to change the password or delete the file.
- `vh user update --disable|--enable`; `vh user create` creates the user's default vault, as the web console does.
