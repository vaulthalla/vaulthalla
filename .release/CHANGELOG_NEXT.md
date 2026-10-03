## Security
- One authorization path for CLI and web: users, roles, groups, vaults, API keys,
  the S3 gateway, price budgets and settings run through shared core/ops
  operations instead of two diverging implementations.
- Close role escalation: admin accounts are judged by what their role grants,
  nobody assigns or manages a role above their own, and the super admin can't be
  renamed or have its password reset by others.
- Deleting, deactivating, re-roling or resetting an account ends its sessions;
  deactivated accounts can't log in; revoked refresh tokens no longer rehydrate
  sessions; logins no longer read a stale user cache.
- No universal admin password: a new database gets a random per-install password
  for the web `admin` account, written once (0600) to
  /var/lib/vaulthalla/super_admin_initial_password. Upgrades replace a leftover
  `vh!adm1n` the same way and end admin's sessions; passwords set by operators
  are untouched. The forced password-change gate is gone.
- Refuse SQL in `--sort` on list commands.
- S3 gateway: role grants can't exceed the principal or delegate role
  administration without admin rights; overrides need an explicit effect and
  pattern; bucket existence is not revealed before authorization; vault names
  never resolve to another owner's vault by accident.
- Price budgets need vault edit permission (owners can't drop an imposed
  budget); settings writes are validated on every surface.
- Vault ownership transfer is limited to administrators.

## Runtime
- Every account gets its role's global vault policy (built-in presets; custom
  roles start unprivileged); startup repairs accounts written with an empty one,
  which also left the S3 gateway bucket list empty.
- Vault updates reach the live engine immediately; owner changes need create
  permission for the new owner; key changes need Consume.
- Health status takes a live database probe, so the web console and `vh status`
  agree when PostgreSQL is down.
- Login rate limiting sees real client addresses behind nginx (#125).
- WebSocket handlers run on several threads, so login bursts no longer stall
  every handshake (#132); the session sweeper no longer closes connections still
  in their handshake (nginx 502s).
- Stopping the S3 gateway or the HTTP preview service waits for open connections
  instead of freeing state they still use.
- Changing s3_gateway.enabled through settings restarts the gateway.
- New migration 098 (auth_bootstrap_state).

## CLI
- Deleting a user asks first and transfers or destroys their vaults:
  `vh user delete <user> [--transfer-to <user>] [--yes]` (#133).
- `vh setup set-super-admin-password` changes the web admin password; only the
  Linux user bound as the super admin may run it.
- `vh setup nginx` warns before exposing the console while the generated admin
  password and its plaintext file remain, and offers to rotate it or delete the
  file.
- Unknown options are rejected instead of silently ignored.
- Role permission flags, `role admin create --from` and vault role overrides
  work as documented; `--from` copies the source role's permissions.
- `vh user update --disable|--enable`; `vh user create` creates the default
  vault like the web console.

## Web console
- Delete-user dialog with the same confirmation and a transfer target picker;
  vault owner chosen by username.
- Role form "Start from" copies an existing role's permissions.
- The super admin sees a warning while the generated initial password and its
  file remain; sessions are never blocked.
- Encryption waiver confirmation for S3 vault changes over existing data.
- Role permission editing applies the full permission set.

## Packaging
- Release tooling moved to vl-release; Debian changelog and release notes are
  written ahead of each release instead of generated in CI.
- Install and postinst summaries say where the generated admin password is.
