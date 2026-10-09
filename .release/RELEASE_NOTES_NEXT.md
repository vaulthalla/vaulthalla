<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Security and stability fixes

This release fixes authorization, account lifecycle and filesystem-cache defects found while provisioning an
authorized penetration-test fixture, plus the open log-hygiene and API-consistency bugs.

### Sharing and authorization

- **Public share links work on any folder or file, not just the vault root.** The share permission check evaluated
  the share path in the wrong path space, so every subfolder or file target was refused with
  `missing_share_public_permission`. In the same path, a share path that matched another vault's mount name was
  judged against that other vault's root. Links are now checked against the vault they belong to, the filesystem
  permission check refuses any entry from a different vault, and a link whose `root_entry_id` doesn't match its
  path, vault or type is refused when it is created.
- **The mount no longer confirms that hidden paths exist.** Inside a vault, a path you have no permission to see now
  consistently answers `No such file or directory`, whichever operation reaches the daemon first. Paths you can see
  but can't act on still answer `Permission denied`.
- **The mount no longer serves cached metadata across users.** The kernel caches lookups and file attributes for all
  users of the mount, so a user without access could `stat` a path another user had just opened (for up to a minute
  after it was created). Every lookup and `stat` is now checked for the calling user. Files you may download are now
  also visible to you in lookups and directory listings even when your role grants no preview.

### Accounts

- **Deleting a user no longer fails** for anyone who has shared, used a share link, uploaded into someone else's
  vault, kept file versions or held a lock. Attribution and audit records keep their history without the identity
  link. File locks and the public share links the user created are removed with the account.

### Filesystem

- **Recreating a vault with a deleted vault's name starts empty.** The filesystem cache kept a deleted vault's
  entries, so uploads into a new vault with the same name failed with `Upload target already exists` until the
  daemon restarted. Renaming a vault also moves its cached entries to the new mount name.

### Logs and operations

- Rate-limited and unauthorized web-console requests log one warning per client and command per minute, plus a
  "suppressed N more" summary, instead of one line per request. The rate-limit message names the right limiter.
- Expected refusals (permission denied, not found, invalid input) are no longer logged as database errors, and a
  failed refresh-token check logs its reason.
- Stopping or upgrading the package no longer marks `vaulthalla-web.service` as failed.

### Breaking: S3 gateway and pricing WebSocket payloads

- `s3.gateway.credentials.*` commands no longer accept a bare `id`. Name the credential with `credential_id` (or
  `access_key` / `name`), a role override with `override_id`, and a permission with `permission_id` (or
  `permission_qualified` / `permission_name`). A payload that carries `id` is refused with an `invalid` error naming
  the expected field. The bundled web console already sends the explicit fields. `vh s3-gateway creds role override
  remove` also takes `--override-id`.
- The same rule now applies to pricing: `pricing.budget.override.approve` / `.deny` take `override_id` and
  `pricing.notifications.ack` takes `notification_id`. A bare `id` (or a missing field) is refused with `invalid`.
