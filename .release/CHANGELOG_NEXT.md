<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
## Security
- rbac: new `admin.stats.view` (admin_role.stats_permissions, bit 0) gates every system stats command, the dashboard
  and severity instead of User::isAdmin(); vault stats need the vault's owner or admin view + view_stats on it
  (ops::stats) (#166). Other isAdmin() sites are unchanged and listed on #166 for follow-up.
- share: `sharing.enabled`, `enable_anonymous` and `enable_email_validated` are enforced on link create/update and
  on every use (public open, email challenge, principal resolve, TargetResolver resolve and listed children), so
  cached HTTP principals and live ws share sessions stop too; management (get/list/revoke/rotate) is never gated (#164).
- fs copy: each entry needs Copy and Read on its source and Write (file) or Touch (folder) at its destination (#167).

## Runtime
- sync: under `ask`, per-file sync baselines (sync_file_baseline) tell one-sided changes (synced) from two-sided
  ones (one open conflict per file, auto-closed on convergence); resolution keep_local/keep_remote refuses stale
  decisions, uses price preflight and the vault's request budget, and holds no lock across network work (#187).
  - psql 107: vault.sync.action.resolve_conflicts granted wherever sync trigger is held (vault_role,
    user_global_vault_policy); sync_conflicts gains vault_id/updated_at, event_id ON DELETE SET NULL, one open row
    per file (older duplicates closed as superseded); artifacts gain remote_etag/encrypted.
  - ws sync.conflicts.{summary,list,resolve}; HTTP GET|HEAD /download/conflict (≤ 32 MiB, If-Match pinned).
  - config: vaults.s3.default_remote_conflict_policy defaults to ask.
- vaults: deleting a vault schedules it (psql 106: vault.deleted_at, vault_deletion, vault_deletion_key, vault_keys
  export tracking). Deleted vaults leave every read path at once and are restorable until vaults.retention_window
  (5m); VaultRetentionService then purges upstream objects (when chosen; bounded per pass, resumable), the backing and
  cache directories (path-guarded) and the vault row. Sealed keys are kept for vaults.tpm_retention_window (90d) /
  vaults.s3.tpm_retention_window (180d), never shortened by delete-now; a tombstone stays (#162).
  - NeedsConfirmation codes vault_upstream_key_loss and vault_delete_now; ws storage.vault.remove.plan,
    storage.vault.deleted.list, storage.vault.restore.
  - sync: pruneStaleTasks no longer indexes a bitset by MAX(vault.id); list queries AND a filter onto an existing
    WHERE; listUserVaults quotes its type literals.
  - users: deletion refuses while the account's API keys are bound to any vault, deleted ones included.
- psql 105: add admin_role.stats_permissions and grant view to admin, auditor, platform_operator, super_admin and
  any role that passed the old gate; register `admin.stats.view` in the permission catalog (#166).
- config: `sharing.enable_public_links` renamed `enable_email_validated`; new `enable_anonymous`;
  `s3_gateway.default_remote_*` moved to `vaults.s3.*` and used as defaults for new S3 vaults. Old keys are read
  as deprecated aliases (new key wins, one warning at startup); settings saves write the new keys (#164).
- ws: cap concurrent connections at `websocket_server.max_connections` (503 + Retry-After); sessions failing before
  the handshake close immediately; new `settings.policy.get` for any signed-in user (#164).
- fs: deleting a file keeps its parent folders on every path (console, FUSE, S3 gateway, sync, trash purge); FUSE
  rmdir returns ENOTEMPTY/ENOTDIR; sync DeleteLocal removes the file's real backing path (#168).
- fs: `fs.entry.copy` is deep, copies sealed bytes at the alias backing layout under the source's content lock,
  checks quota, is all-or-nothing, and hydrates cloud-only sources first; the dead sync operations replay is
  removed; recursive cache listings reach every depth (#167).
- http: folder downloads stream a STORE ZIP (ZIP64, UTF-8 names, data descriptors, exact Content-Length); each
  member authenticates before its CRC is written. The 256/320 MiB and 4096-entry caps and the two-archive limit are
  removed; a 50,000-entry walk limit returns 413. Share folder ZIPs count one download per GET (#143).
- stats: short trend windows read the 5-minute rollups; overview hrefs are console routes, money and counts carry
  numeric values, cards carry at most 3 series of 64 points; unmeasured spend and FS cache capacity are null (#160).

## CLI
- `vh sync resolve` / `vh resolve`: interactive session, `--list [--vault] [--json]`, `<id...> --keep-local|--keep-remote`,
  `--vault X --all --keep-* [--yes]`; `--allow/--deny-sync-action-resolve_conflicts` on vault roles (#187).
- `vh vault delete [--now] [--delete-upstream|--keep-upstream] [--accept-key-loss] [--yes]`, `vh vault deleted`,
  `vh vault restore`; `vh vault keys export` records the exported key version and exports a deleted vault's retained
  key (#162).
- `vh role admin create|update --allow-stats-view|--deny-stats-view` (#166).
- `vh vault create` uses the `vaults.s3` defaults when no sync strategy or conflict policy is given (#164).

## Web console
- Sync Conflicts top-bar button and System page (hidden at zero): select-all, vault filter, bulk Keep local /
  Keep remote, side-by-side preview with a lazy text diff (#187).
- icons: SVGR sets fill="currentColor" through svgProps (the `fill` option it was given doesn't exist, so every
  icon painted black) and marks icons data-vh-icon; a base-layer rule makes them cyan, filled buttons and toned
  containers pass their own color (#188).
- One vault delete dialog (upstream choice, key warning, delete / delete now), a Deleted vaults panel with Restore and
  Purge now, and vault retention settings (#162).
- Health, the health dot and system storage sizes follow `admin.stats.view`; the roles editor lists "Health and
  stats"; the console uses the daemon's hrefs, money and counts instead of parsing display text (#166, #160).
- Share UI follows the sharing policy; the new-vault form uses the vault defaults; the settings editor gains the
  sharing switches and a vault defaults section (#164).
- Folder download tasks show the ZIP size and name an entry-limit refusal (#143).

## Tests
- SyncConflictsTest (baselines, one-sided changes, convergence, both resolutions, RBAC), ConflictParityTest,
  migration 107 upgrade case; VaultRetentionTest, VaultParityTest deletion lifecycle; StatsAccessTest, SqlDeployerHistory migration 105, role parity stats bits; config/settings round trips and
  aliases, WsConnectionLimit, share policy refusals; FsCopyDbTest, keep-folder FsDirStats cases; HttpArchiveZip
  (validated with Python zipfile and unzip -t), share folder ZIP accounting; harness stage "Copy And Delete".
