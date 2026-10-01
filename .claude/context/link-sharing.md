# Link sharing

Merged to `main` in PR #61 (`7786acae`). Later HTTP upload/download convergence landed with PR #78.
The full per-PR history (44 specs) lives in the gitignored `.claude/scratch/link_share/`. Read it only
for archaeology, because parts of it are stale (for example, it says uploads stay on `/ws/share`, which is no longer true).

## Model

- Public/email-validated recipients are constrained **`rbac::Actor::Share`** actors
  (`rbac/Actor.hpp`, `Kind{Human,Share}`). They are never synthetic users.
- Share links assign **persisted vault role templates** to `public` or `actor` subjects, with
  per-assignment filesystem overrides (`072_share_vault_role_assignments.sql`).
  `link_share_vault_role` is legacy and read-only.
- Core: `core/include/share/*`: `Manager`, `TargetResolver`, `PrincipalResolver`, `Scope`, `RateLimiter`.
- Web: `/share/[token]` renders the regular `components/fs` path. `vaultShareStore` holds **only**
  bootstrap, session, and email state, while `fsStore.ts` owns filesystem state. Share ws is in `useShareWebSocket.ts`.

## Invariants

- Share sessions never set `Session::user`. Admin/superadmin bypass is human-only (`canUseHumanPrivileges()`).
- ws Router allowlists are exact per session mode. Adding a share-callable command means an explicit allowlist edit.
- Link creation (`canGrant`) requires the human creator to hold every delegated filesystem permission.
- **Overwrite goes through filesystem RBAC `FilesystemAction::Overwrite`** and must never become a
  share-only boolean or shortcut (`Grant.cpp`).
- Public preview and download use `/preview?share=1` and `/download?share=1` with the `Secure` `share_refresh` cookie.
  Raw tokens never go in URLs. Manual QA therefore needs the HTTPS origin (Caddy, `https://vh.home.arpa:8443`).
- Uploads use the HTTP lane `/upload/session?share=1` (`fsStore.ts`, `http/upload/Coordinator.cpp`).
- Directory thumbnails use scoped HTTP preview, never per-row ws preview.
- Upload-only ("dropbox") shares never list or preview beyond what they were granted.

## Open items (verified 2026-09-30)

1. **Email-challenge codes are never delivered.** `share::Manager::startEmailChallenge` returns a
   `verification_code`, but the ws handler drops it and nothing calls `email::Provider`. Email-validated shares
   can't be completed by a real recipient. This is the highest-value gap, and the email providers already exist.
2. Actor-native RBAC is incomplete. `rbac/resolver/vault/Context.hpp` and `rbac/fs/policy/Request.hpp`
   still carry `shared_ptr<identities::User>`.
3. `fs.dir.create` isn't in the share allowlists, so share mkdir is unsupported.
4. Duplicate policy: `rename` and `overwrite` throw "not supported in this pass"
   (`ws/handler/share/Upload.cpp`, `http/upload/Coordinator.cpp`); only `reject` works.
5. The legacy `share.fs.*`, `share.download.*`, `share.upload.*`, and `share.preview.get` ws commands are still
   registered but unused by the frontend. They can be retired.
