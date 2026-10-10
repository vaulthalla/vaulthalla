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
- psql 105: add admin_role.stats_permissions and grant view to admin, auditor, platform_operator, super_admin and
  any role that passed the old gate; register `admin.stats.view` in the permission catalog (#166).
- config: `sharing.enable_public_links` renamed `enable_email_validated`; new `enable_anonymous`, `enable_internal`
  (reserved); `s3_gateway.default_remote_*` moved to `vaults.s3.*` and used as defaults for new S3 vaults. Old keys
  are read as deprecated aliases (new key wins, one warning at startup); settings saves write the new keys (#164).
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
- `vh role admin create|update --allow-stats-view|--deny-stats-view` (#166).
- `vh vault create` uses the `vaults.s3` defaults when no sync strategy or conflict policy is given (#164).

## Web console
- Health, the health dot and system storage sizes follow `admin.stats.view`; the roles editor lists "Health and
  stats"; the console uses the daemon's hrefs, money and counts instead of parsing display text (#166, #160).
- Share UI follows the sharing policy; the new-vault form uses the vault defaults; the settings editor gains the
  sharing switches and a vault defaults section (#164).
- Folder download tasks show the ZIP size and name an entry-limit refusal (#143).

## Tests
- StatsAccessTest, SqlDeployerHistory migration 105, role parity stats bits; config/settings round trips and
  aliases, WsConnectionLimit, share policy refusals; FsCopyDbTest, keep-folder FsDirStats cases; HttpArchiveZip
  (validated with Python zipfile and unzip -t), share folder ZIP accounting; harness stage "Copy And Delete".
