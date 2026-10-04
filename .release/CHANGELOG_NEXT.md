<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->

## Web console
- Rebuild the console on one design system (dark tokens, components/ui
  primitives), a single permission-filtered shell with a command palette,
  and a typed ws client (connect deadline, backoff with jitter, per-command
  timeouts, typed refusals) with a TanStack Query cache over ws.
- Keep the access token in memory only (refresh cookie re-issues it); clear
  all session state on logout; show a reconnecting state instead of
  redirecting to login when the daemon is unreachable.
- Files: one FileBrowser for vaults and share links (URL-addressed paths,
  virtualized list/grid, keyboard, row and context menus, upload button,
  multi-item drop, transfer manager with session splitting, retry, cancel and
  a beforeunload guard, download preflight); fixes a late listing landing on
  the wrong vault.
- Vault detail as tabs (overview, access incl. unassign and overrides,
  shares, sync & cost, gateway, settings); Health replaces the dashboard and
  never renders unknown data as healthy; pollers stop when hidden or left.
- Admin pages: user active flag saved as a boolean, explicit update payloads,
  roles from roles.admin.list, confirmation for every destructive action.
- Hard per-route first-load JavaScript budgets in CI (web/perf-budgets.json);
  design-token color guard and jsx-a11y rules in pnpm test.
- Redirect old console URLs to their new routes.

## Runtime
- Fix the sync controller spinning one CPU core at 100% whenever the next
  sync was scheduled in the future (always, once a vault had synced): it now
  sleeps until the earliest sync is due, a sync is queued, or the service
  stops.
- Engine::getVaultSize walks the vault's own backing tree
  (backingPath/<mount_point>) instead of the shared backing root, so
  stats.vault physical_size, stats.system.storage vault_size_bytes and the
  quota check in freeSpace() (HTTP/share uploads, sync, key rotation) count
  only that vault; a missing tree is 0 and files vanishing mid-walk are
  skipped instead of throwing.
- Keep directory subtree totals (size_bytes, file_count,
  subdirectory_count) on both ancestor chains for fs.entry.move/rename
  across directories (Directory::shiftSubtreeTotals), for deletes of empty
  directories (Directory::deleteDirectoryTree) and for directories the
  delete cleanup cascades; a shallow directory copy starts at zero; the fs
  cache re-reads affected totals (Registry::refreshDirStats).
- Filesystem::rename of a directory walks the directory's own subtree,
  shallowest first (it walked the parent's, so any sibling aborted it with
  EIO); collect_parent_chain orders ancestors by distance instead of
  parent_id, which gave wrong fuse/backing paths for entries moved under a
  newer directory.

## Database
- Run every daemon DB session with TimeZone=UTC (recording the session's
  original zone as vaulthalla.database_timezone) so naive timestamps are no
  longer local wall time read back as UTC on non-UTC servers.
- Migration 100 converts every timestamp column to timestamptz, reading
  existing values in the recorded database zone (session zone for manual
  runs); idempotent, keeps defaults and indexes, skips columns a view
  depends on with a warning, and avoids the table rewrite when the zone is
  UTC.
- Migration 102 adds users.password_changed_at (TIMESTAMPTZ, NULL for
  existing rows) and a trigger that stamps it whenever password_hash
  changes (or on insert with a password), covering ws self change, admin
  reset, CLI, set-super-admin-password, bootstrap and seed; loaded with
  the user, so auth.user.get no longer returns null after a reload.

## API keys
- Add storage.apiKey.update: edits a key in place (same id, so vault s3
  bindings survive), keeps the sealed secret when none is given, re-checks
  credentials with the provider and reloads the engines of the vaults using it.
- Refuse removing an API key while a vault uses it (ops Invalid naming the
  vaults, CLI and ws), and refuse deleting a user whose key a surviving vault
  uses. Migration 099 changes the s3.api_key_id foreign key from ON DELETE
  CASCADE to ON DELETE RESTRICT.
- storage.apiKey.list returns keys as a JSON array instead of a JSON-encoded
  string.

## Web console API
- ws ERROR responses for ops refusals carry data.code ("denied", "not_found",
  "invalid", "conflict"); admin gates in the stats, settings, email, pricing
  and share upload handlers now raise typed denials.
- auth.users.list returns a slim projection: admin and vault roles without
  their permission sets; auth.login, auth.refresh and auth.isAuthenticated
  return the session user's permissions as {qualified, value} only.
- storage.vault.list rows carry owner (the owner's name, as
  storage.vault.get does) next to owner_id.
- Add stats.dashboard.severity (overall status and counts without the
  dashboard cards) for the console's status badge.
- Dashboard metric tones never report an unmeasured value as healthy
  (slow_queries, connections errors_24h -> unknown); oldest_tx compares
  the age with the DbStats thresholds (warning >= 1 h, error >= 24 h) and
  excludes the stats query's own transaction; errno_types warns only for
  errnos with alertable occurrences (FuseStats now tracks alertable_count
  per errno and alertable_errno_types).
- Add ws commands role.vault.overrides.{list,add,update,remove} for
  per-assignment, path-scoped vault permission overrides (the same ops and
  RBAC as vh vault role override ...).
- pricing.notifications.list always returns summary {open_count,
  worst_severity} over the caller's open (unacknowledged, unexpired) alerts,
  from one aggregate query with the rows' vault visibility, independent of
  limit; the cost-alerts bell requests limit 8 and badges from it. (#172)
- Contract test: core ws registrations must match web WebSocketCommandMap.

## Build
- The core builds with no compiler output but progress at -O3 -Werror,
  unity and per-file: OpenSSL 3 EVP digests replace the deprecated MD5_* /
  SHA256_* calls; default member initializers; uninitialized fds in the
  integration harness; a missing <ostream> hidden by unity; system() results
  checked. GCC builds force-include core/include/compat/gcc_variant.hpp for a
  libstdc++ <variant> -Wmaybe-uninitialized false positive via libpqxx.
- Packages: binaries are built at -O3 with hardening=+all (FORTIFY_SOURCE=3,
  stack protector, stack clash and CET protection, relro/now) and -Werror;
  1.8.x shipped -O0 and unhardened because a cpp_args default_option made
  meson drop dpkg-buildflags. The ABI define is now a project argument and the
  default buildtype is explicitly debug. No LTO.
- CI: the PR gate builds at -O0 with -Werror and runs the suite; release CI no
  longer rebuilds and re-tests (core-verify removed) and checks the package's
  compile lines with tools/dev/check_build_flags.py.
- Compile time: unit-test objects build first (own target, linked whole) and
  always at -O0; -O0 builds use precompiled headers; fmt 9's vformat_to<char>
  is an extern template; fmt::format replaces std::format in the RBAC
  templates; spdlog sinks, <regex>, shell argument helpers and all of asio left
  widely included headers; heavy inline code moved to .cpp files.
- Forward declarations: subsystem Fwd.hpp headers (identities, auth, storage,
  vault, fs, rbac, share, sync, crypto, protocols/ws, protocols/shell) and
  libpqxx's <pqxx/types> replace 355 repeated local declarations.

## FUSE
- FUSE is the decrypting view of at-rest ciphertext (#173): open decrypts
  each inode into a shared 0600 working copy under
  <backing>/.fuse-plaintext; reads/writes/truncate use it; flush, fsync and
  the last release seal it back (new IV, fsynced temp + rename) and record
  the plaintext size_bytes. Copies that fail to seal move to unsaved/;
  stale copies are removed at mount.
- Same-vault renames only move the ciphertext (canFastPath no longer
  requires an IV); the cross-vault path records the plaintext size.
- Filesystem::repairAtRest (first sync pass per vault per start) seals
  plaintext files left by older builds and corrects ciphertext-length sizes.
- Stop requesting FUSE_CAP_WRITEBACK_CACHE: with it the kernel ignored
  getattr sizes, so out-of-band changes kept stale st_size; every handle is
  direct_io, so it cached no data.
- FUSE forget no longer evicts metadata cache entries: evicting a vault root
  made path lookups under it fail with ENOENT until restart.
- open requires Write for writable or O_TRUNC handles (Read and Write for
  O_RDWR); write checks Write before writing; setattr size works.
- Streaming AES-256-GCM file decryption (crypto::util::decrypt_aes256_gcm_file,
  EncryptionManager::decryptFileToFile); decrypt_file_to_temp creates its
  plaintext temp files O_EXCL 0600.

## Auth
- Refresh tokens (human and share) are stored as sha256:<hex> digests
  instead of Argon2 hashes; verification accepts both and rewrites a legacy
  row on its first successful check (conditional UPDATE). A session check
  drops from ~0.57 s to well under a millisecond, which also speeds up HTTP
  preview, download and upload auth. No migration. (#171)
- Web middleware only checks that a refresh cookie exists; the websocket
  session gate decides validity. Removed /api/auth/session and
  src/lib/server/authCheck.ts. (#171)

## HTTP
- Upload sessions use a sliding 30-minute idle TTL (refreshed by every
  request and body chunk of the owning session) with a 24-hour ceiling,
  instead of expiring 30 minutes after creation.
- Content-Disposition carries an RFC 5987 filename* (UTF-8) next to an ASCII
  filename fallback, and no longer strips leading dots.

## Link shares
- Migration 101 makes share_upload.target_parent_entry_id nullable and
  ON DELETE SET NULL, so folders that received share uploads can be deleted
  (previously a foreign key violation); upload history rows survive.
- Seed share_upload_dropbox without directory List on new installs; share
  uploads need only the upload operation. Existing role rows are unchanged.
