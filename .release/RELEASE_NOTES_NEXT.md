<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->

# A rebuilt web console, readable files on the mount, correct times on non-UTC servers

## The web console, rebuilt

The web console has a new look and a new structure. It keeps the dark theme with cyan accents, now with one
consistent visual language on every page, and it is much lighter: pages load about a third less JavaScript,
and the console only asks the daemon for what the page on screen needs.

- **One navigation for everything.** A single sidebar groups the console into Files, Shares, Vaults, Access
  (users, groups, roles), Storage & cost (provider credentials, cost control, S3 gateway) and System (health,
  notifications, settings). It only shows what your role can use. Press `⌘K` / `Ctrl+K` anywhere to jump to a
  page, vault, user or action.
- **Files.** Upload with the new Upload button (files or whole folders) or drop several files and folders at
  once; every dropped item now arrives. Rename, move, copy and delete from a menu on each row, by right click, or
  from the keyboard (arrows, Enter, F2, Delete, Ctrl+A); deleting always asks first. Folder paths are part of the
  address, so back, refresh and links to a folder work. Large folders stay fast. Transfers show progress, speed
  and time left, can be cancelled, retry briefly interrupted files, and the browser warns before you close a tab
  mid-upload. A refused download (for example over the size limit) is reported in the transfers panel instead of
  replacing the page.
- **Vaults** open as one page with tabs: overview (live stats), access (assign, change and remove roles for users
  and groups, plus path-scoped permission overrides), shares, sync & cost, gateway and settings.
- **Health** (formerly the dashboard) shows only what the daemon reports: when a value is unknown or the daemon
  is unreachable it says so, and never shows green. Polling stops when you leave the page or hide the tab.
  The daemon's own health ratings were corrected too: a value it can't measure (slow queries without
  `pg_stat_statements`, connection errors) is rated unknown instead of healthy, the oldest database transaction
  warns only after an hour (it used to warn on every idle install), and FUSE errors that are part of normal
  operation (a lookup of a name that doesn't exist yet) no longer raise a warning.
- **Shares.** Recipients get the same file browser, with paths in the address. The new "Upload dropbox" preset
  lets people send files into a folder without seeing what's already there. Rotating or revoking a link asks first.
- **Safer by default.** Every destructive action asks for confirmation. Logging out clears everything the browser
  held for the session, and the access token is no longer stored in the browser. When the daemon restarts, the
  console says it is reconnecting instead of sending you to the login page.
- **Fixes:** users can be deactivated again (choosing "Inactive" used to save the user as active); editing a user
  no longer fails; any admin role can be assigned; "Last login" shows "Never" instead of 1969; the version in the
  sidebar is the real one; vault owners show on the vault list; light-mode browsers no longer get an unreadable
  login page.
- **Password age is recorded.** The daemon now stores when each account's password was last set (by the user,
  by an admin reset, on the CLI or with `vh setup set-super-admin-password`), so the console can show it. It was
  always empty before. Accounts whose password hasn't changed since the upgrade show no date.
- Old console addresses (for example `/dashboard`, `/api-keys`, `/pricing-budget`, `/operator-email`) redirect to
  their new pages.

## Files on the mount are always readable, and always encrypted on disk

Vault files are stored encrypted on disk, and the mount at `/mnt/vaulthalla` is where you read them in plain form.
Until now the mount handed out the stored bytes as they were. Any file uploaded through the web console, synced
down from S3, or renamed or moved (on the mount or in the console) read back as **encrypted bytes** with `cat`,
editors, `tar` or `rsync`. Files written through the mount were left **unencrypted** on disk until something renamed
them. Web downloads were never affected.

- The mount now decrypts files when they are opened and encrypts changes when they are closed or synced
  (`fsync`). `close` returns once the change is encrypted on disk, so the console shows it right away. Renaming or
  moving a file no longer re-encrypts it.
- `ls -l`, `stat` and `du` report the real file size. They used to show 0 for files written through the mount and
  the encrypted size (16 bytes more) for others, after a restart.
- `truncate` works on the mount. It used to be silently ignored.
- Opening a file for writing now needs write permission on it, and a refused write changes nothing. A user with
  read-only access could open a file for writing, and a refused write had already been written to disk.
- **On the first start after the upgrade** each vault's files are checked once: files left unencrypted are
  encrypted, and wrong sizes are corrected. This takes longer on vaults with many files written through the
  mount, and an S3 vault may upload those files once more. Nothing needs to be done by hand.
- Temporary decrypted copies (for downloads and previews) are now readable only by the daemon.

## Faster page loads, previews and downloads

Every check of a signed-in session cost the daemon about half a second, because session tokens were stored with
the same slow hash as passwords. A console page load paid that twice, and every preview, download and upload
paid it once. Session tokens are now stored as a SHA-256 digest, which is the right tool for long random values
the server mints itself, so the check takes well under a millisecond. Existing sessions keep working: each
stored token is upgraded the first time it is used.

The console also stopped asking the daemon about the session before loading every page. It only checks that you
have a session cookie; the console then confirms the session over its connection, sends you to the login page if
it was revoked, and shows its reconnecting state (not the login page) while the server is unreachable.

The cost-alerts bell is lighter and more accurate. It fetches the 8 newest open alerts and a summary from the
server instead of 50 alerts every minute, and its count and colour cover every open alert you can see, so an
older critical alert is never hidden. When more alerts are open than shown, "View all N alerts" opens Cost
control.

## Times on servers outside UTC

On a server whose PostgreSQL time zone isn't UTC, most times the daemon reported were off by the UTC offset: a
file uploaded a minute ago showed as "7 hours ago" on a Mountain Time host, and `vh user list` printed local times
marked as UTC. The daemon now talks to PostgreSQL in UTC, and the upgrade converts every stored time to a
time-zone-aware value.

- **Before you upgrade:** the upgrade reads times already in the database as the database's current time zone
  (`SHOW timezone` in `psql`). If you changed that setting after installing, times written before the change stay
  off by the difference.
- The first start after the upgrade rewrites the tables that hold times once, so it takes longer on large
  installs. Servers already on UTC skip the rewrite.

## API keys

- Editing an API key in the web console now changes the key in place. It keeps its id, and every vault that
  uses it keeps its S3 binding. Saving an edit used to delete the key and create a new one, which silently
  removed the bucket binding (and upstream encryption setting) from every vault using the key. Leave the secret
  empty to keep the stored one. New credentials are checked against the provider before they are saved, and
  running vaults switch to them right away.
- Removing an API key that a vault still uses is refused, on the CLI and in the web console, and the message
  names those vaults. Deleting a user is refused the same way while another vault still uses one of their API
  keys. The database enforces this as well: a schema migration changes the vault-to-key reference from
  "delete the vault's binding" to "refuse".

## Uploads and downloads

- Browser uploads no longer fail after 30 minutes. An upload session used to expire 30 minutes after it
  started, so long uploads on slow links failed at the end and lost their partial data. A session now stays
  open while data keeps arriving, expires after 30 minutes without activity, and ends after 24 hours at most.
- Downloads keep their file names. Names with non-ASCII characters (`naïve 日本.pdf`) arrive intact in current
  browsers, and names that start with a dot (`.env`) keep the dot instead of downloading as `env`.

## Link shares

- Folders that received uploads through a share link can be deleted again. Deleting such a folder (or anything
  above it) used to fail with a database error; the link's upload history is now kept without its folder.
- The "Upload Dropbox" share role is upload-only on new installs: recipients can upload but can't list the
  folder, so they don't see each other's submissions. Existing installs keep their `share_upload_dropbox` role
  unchanged. To make it upload-only there too, run `vh role vault update share_upload_dropbox --deny-dirs-list`.

## Vault permission overrides

Path-scoped allow/deny overrides on a vault role assignment (for example "deny downloads under `/finance/**`")
can now be listed, added, changed and removed from a vault's Access tab, not only with `vh vault role override`.

## Folder sizes and item counts

- Moving or renaming a file or folder into another folder now updates the size and item count of both folders
  (and the folders above them), as uploads always did. A folder that received files by a move used to show
  "0 items", and the folder they came from kept counting them. Copies and deletes update them too.
- Moving or renaming a folder into another folder now works when the folder it leaves holds anything else. It
  used to fail with an I/O error. Files inside a folder moved under a newer folder keep working: their location
  on disk was worked out from the wrong folder order.
- Deleting an empty folder (or one holding only empty folders) from the web console removes it. It used to stay
  listed after its contents on disk were gone.
- Counts that are already wrong on an existing install are not recomputed by this release.

## Vault sizes and quotas

- Each vault now reports its own size. Every vault used to report the size of the whole storage directory,
  which is mostly the default vault's data, so an empty vault (or an S3 vault with nothing cached locally) showed
  the default vault's usage in the vault overview, the Health storage view and `stats.vault`.
- Vault quotas are checked against that vault's own data. A vault with a quota used to be charged for every other
  vault's files and the preview cache, so uploads and syncs into it could be refused as over quota while it was
  nearly empty.
- For an S3 vault the reported size is what is stored on this server (the local cache), not the bucket's total.
- The vault list now includes each vault's owner name.

## Idle CPU

The daemon no longer keeps one CPU core busy while it waits for the next vault sync. A background loop
re-checked the sync schedule nonstop, so an idle server showed a constant load of about 1.0 with one core at
100% (1.8.0 and earlier). It now sleeps until the next sync is due and still starts on-demand syncs
immediately.
