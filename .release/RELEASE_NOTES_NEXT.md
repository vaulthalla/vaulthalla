<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Health access, sharing controls, deep copy and streaming folder downloads

This release resolves the open product decisions on stats access, sharing settings, folder deletion and copying, and
makes folder downloads stream at any size.

### Upgrade notes

- **Two settings were renamed.** `sharing.enable_public_links` is now `sharing.enable_email_validated`, and
  `s3_gateway.default_remote_sync_strategy` / `default_remote_conflict_policy` moved to `vaults.s3`. Your
  `config.yaml` is not rewritten: the old names keep working and the daemon logs a deprecation warning at startup.
  Rename them when convenient; saving settings from the console writes the new names.
- **Health has its own permission.** Migration 105 adds "view stats" (`admin.stats.view`) and grants it to the admin,
  auditor, platform operator and super admin roles, plus any custom role that could already see Health.
- **`rmdir` on the mount refuses a folder that still has contents** (`ENOTEMPTY`). It used to delete the contents.
- **Copying needs create permission where the copy lands** and download permission on what is copied. The built-in
  `reader` role (no upload) can no longer copy.

### Health and stats

- The built-in admin role can open Health and see the health dot. Health, the health dot and every server, daemon
  and system statistic follow `admin.stats.view` instead of a full-admin check.
- A vault's owner sees that vault's statistics without an admin role; other people's vaults stay private.
- The 24-hour trend view reads the same 5-minute data as the 7-day view, so it is never larger than the week.
- Unknown values are shown as unknown: cost figures without a monthly budget are no longer a fake $0.00, and the
  file-metadata cache no longer reports "0 B of 0 B". The preview cache reports its configured size from startup.

### Sharing and settings

- Operators can turn share links off: `sharing.enabled` turns off every link, and `sharing.enable_anonymous` and
  `sharing.enable_email_validated` turn off one kind each. Links already handed out stop working while their switch
  is off and work again when it is turned back on. The console hides what is turned off. `sharing.enable_internal`
  is reserved for a future link kind.
- New S3/R2 vaults take their sync strategy and conflict policy from `vaults.s3.default_remote_sync_strategy`
  (`cache`) and `vaults.s3.default_remote_conflict_policy` (`keep_local`) when none is given, from the CLI or the
  console.
- `websocket_server.max_connections` (default 1024) now limits open console and share connections. Connections over
  the limit get `503` and the console reconnects.

### Files

- Deleting a file no longer deletes the folder it was in or that folder's parents, in the console, on the mount,
  through the S3 gateway or by sync. Delete the folder itself to remove it.
- Copying a folder copies everything inside it, and every copied file opens immediately (copied files used to have
  no content until a sync pass that never ran). Copies are checked against permissions for every item and against
  the vault's quota, and a failed copy leaves nothing behind.
- Folder downloads stream as a ZIP at any size: the 256 MiB and 4096-file limits are gone and the browser shows the
  archive's size and progress from the start. File names keep their Unicode characters and leading dots inside the
  ZIP. Folders with more than 50,000 items are refused with a clear message in the transfer panel, and a file that
  can't be read mid-download fails the download instead of saving a damaged archive.
- On share links, a folder download counts as one download, and the console's pre-download check never counts.
