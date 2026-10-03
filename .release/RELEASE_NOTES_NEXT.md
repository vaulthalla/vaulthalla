<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->

# Correct times on non-UTC servers, safer API key edits, idle CPU fix

The daemon no longer keeps one CPU core busy while it waits for the next vault sync. A background loop
re-checked the sync schedule nonstop, so an idle server showed a constant load of about 1.0 with one core at
100% (1.8.0 and earlier). It now sleeps until the next sync is due and still starts on-demand syncs
immediately.

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

- The "Upload Dropbox" share role is upload-only on new installs: recipients can upload but can't list the
  folder, so they don't see each other's submissions. Existing installs keep their `share_upload_dropbox` role
  unchanged. To make it upload-only there too, run `vh role vault update share_upload_dropbox --deny-dirs-list`.
