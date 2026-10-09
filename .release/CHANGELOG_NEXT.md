<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
## Security
- share: evaluate share roots as FUSE paths through the vault's engine; subfolder/file links were refused and a
  root path naming another vault's FUSE root resolved to that vault (#178).
  - Link creation refuses a root_entry_id that doesn't match root_path, vault or target type.
  - rbac: the filesystem evaluator denies an entry from another vault (EntryVaultMismatch).
- fuse: denials answer ENOENT when the caller cannot Lookup the target (the parent for creates) and EACCES
  otherwise, regardless of which op reaches the daemon first; replaces the vault-root-only rule (#170).
- s3 gateway ws: refuse a bare `id` on `s3.gateway.credentials.*` with `invalid`, naming credential_id /
  override_id / permission_id; missing references are typed `invalid` (#165). Breaking for ws clients sending `id`.

## Fixes
- psql 104: users(id) references without a delete action become SET NULL (attribution and audit) or CASCADE
  (file_locks, share_link.created_by), so user deletion no longer hits a foreign key (#179).
- fs cache: evict a vault's entries on remove and rename and before a new vault caches its root, so a reused FUSE
  name can't see a deleted vault's entries (#180); listDir reads the maps under the lock and refuses a missing parent.
- ws Router: throttle "Rate limited" and "Unauthorized access attempt" warnings to one per (client, command) per
  60s window with a suppressed-count summary and a bounded map; fix the share label on non-share commands (#135).
- db: Transactions::exec logs ops::Error refusals at debug; unexpected exceptions still log error (#136).
- http: the refresh-token failure log carries its reason (was a literal `%s`) (#136).
- systemd: SuccessExitStatus=143 on vaulthalla-web.service (#136).
- cli: `vh s3-gateway creds role override remove` takes `--override-id`.

## Tests
- VaultLifecycleRegressionTest (share subpaths, cross-vault paths, user delete, vault name reuse and rename),
  FuseDenial, WsRefusalLog, WsS3GatewayPayloads; harness gains cache-warm FUSE deny cases.
- contracts: no printf placeholders in core log calls; the web unit treats exit 143 as success.
