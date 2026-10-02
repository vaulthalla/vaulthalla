# Phase 2 results (trustworthy CLI/web, v1.8.0 candidate)

Branch `phase2/trustworthy`, PR to `main`. What was proven, and how to re-prove it.

## Lab matrix (vh-storage, 2026-10-02, candidate `vaulthalla_1.8.0-1_amd64.deb` sha256 `359d35a0…`, CI run 37022678594)

| Flow | Result |
|---|---|
| Upgrade published 1.7.0-1 → candidate (`lab-smoke --from-version 1.7.0-1 --pg-restart --reboot`) | pass; candidate sha verified on host; config + providers.env byte-identical |
| Stopped-service upgrade 1.7.0 → candidate | stays stopped; starts healthy; installed `vaulthalla-server` matches the candidate's |
| `apt purge` with mounted data disk | mount kept + emptied (root:root 0755), FUSE unmounted, DB kept with removal instructions |
| Reinstall with `VH_EXISTING_DB_ACTION=adopt` | healthy, accounts kept |
| Fresh install (`VH_EXISTING_DB_ACTION=overwrite`) | healthy; daemon ports loopback-only, nginx :80 |
| Restart with an ownerless vault | healthy; vault listed with owner N/A |
| `parity_smoke.py` (now with the Phase 2 scenario set) | 71/71 |
| `ws_churn.py --rounds 12 --concurrency 6` | 288 connections, 0 client errors, same PID, no sweeper errors |

Candidate 1 (`ce05dc0f…`) showed nginx 502s exactly when the 30s session sweep ran: the sweeper closed sessions
still in their handshake (indexed at TCP accept, no tokens yet). Fixed in `ddc32949`; candidate 2 shows none.

The lab was returned to published 1.7.0-1 afterwards, so the release's own `lab-smoke --from-version 1.7.0-1`
starts from a published package (its apt options don't allow downgrades).

## Lab recipe on 1.8.0+

Operator for the parity smoke without the real admin password: `vh role admin create parity-op --from super_admin`,
`vh user create parity_operator --role parity-op`, keep the `^Password: ` line in a 0600 file, run
`tools/lab/parity_smoke.py --admin-user parity_operator ...`, then delete the user, the role, and the default vaults of
the deleted fixture users (they become ownerless, #133).

## S3/R2

Not exercised: `/etc/vaulthalla/testing/providers.env` holds placeholders (`tools/lab/test_providers.sh check --host
vh-storage`). Real-provider work stays credential-blocked until the maintainer fills it.

## Open after Phase 2

- #133: what deleting a vault owner should do. Every deleted account now leaves an ownerless default vault, because
  `vh user create` creates one like the web always did.
- Vault/API-key resolver scopes still classify owners with `User::isAdmin()` (a strict "full admin" predicate also used
  as a gate for S3 policy bypass and system stats); account management uses `ops::users::isAdminIdentity`.
- Gateway bucket listings (`ObjectStore::listBuckets`) follow the caller's vault filesystem rights; an operator whose
  role was cloned `--from super_admin` lists no buckets even on its own vault. Same on both surfaces; not investigated.
- ws-only endpoints that kept their own authorization: pricing preflight/overrides/notifications, email test-send and
  history.
- Revocation that races a session's in-flight request reads token fields off the strand (pre-existing for CLI-driven
  revocation; more threads make it likelier).
