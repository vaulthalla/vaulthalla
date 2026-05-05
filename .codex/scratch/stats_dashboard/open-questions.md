# Stats Dashboard Open Questions

## Phase 5

- Should FUSE rename/move operations also write `operations` rows, or should they remain represented through file create/modify/delete activity until a duplicate-safe mutation event API exists?
- Should `operations` rows created for sync processing be marked completed after `Local::processOperations()` succeeds? This predates Phase 5 and is not changed here.
- Should `VaultActivity` eventually split human uploads from share uploads? Phase 6 will add share-specific observability.

## Phase 6

- Should public share observability get a global admin card in addition to the per-vault Share Observatory card?
- Should top share events eventually expose redacted/top-limited remote IP or user-agent summaries, or keep those details out of dashboard stats unless explicitly requested?

## Phase 7

- Should slow-query thresholds stay fixed at mean execution time >= 1s, or become configurable from admin settings?
- Should index bloat be estimated in a later phase, or deferred until historical DB snapshots exist?

## Phase 8

- Should vault security eventually expose redacted/top-limited denied access source summaries, or keep those out of dashboard telemetry unless explicitly requested?
- What process should own checksum verification so `integrity_check_status` can move from `not_available` to a real pass/fail signal?

## Phase 8A

- Should `backup_policy` be constrained to one row per vault, or is latest-row-by-id the intended policy selection rule?
- What future process should write a distinct backup verification timestamp so "verified good state" can mean more than last successful backup completion?

## Phase 8B

- Should upload progress writes gain a lightweight `updated_at`/heartbeat timestamp so stalled uploads can be detected by lack of byte advancement instead of age alone?
- Should operation queue retention prune old success rows, or should historical snapshot phases own long-term operation trend storage?

## Phase 8C

- Should websocket lifecycle add atomics for 24h opened/closed/swept/error counters, or should those wait for historical snapshot/event work?
- Should admin dashboards ever expose redacted/top-limited IP/user-agent summaries, and what privacy policy should govern that?

## Phase 8D

- Should provider operation/error/latency counters be added around local and S3 engine boundaries, or deferred until historical snapshots need provider trend data?
- Should `allow_fs_write` become editable in the vault admin UI now that the runtime model carries the schema field?

## Phase 8E

- Should share access events get an independent retention policy, or continue sharing broader audit/log cleanup policy until historical snapshots exist?
- Should cache cleanup pressure distinguish thumbnail cache from file cache once both have separate retention/eviction policies?

## Phase 9

- Should raw snapshots be compacted into daily rollups after 30 days if operators increase retention beyond the MVP default?
- Should trend extraction expand to include storage backend/recovery/security once those cards have stable live fields worth trending?
- Should snapshot cadence be editable through the admin settings UI, or remain file-configured for now?

## Phase 10

- Should the server-rendered admin sidebar get a small client-side dashboard severity badge component in Phase 11, or should nav severity stay inside the `/dashboard` overview surface until customizable layouts land?
- Should overview severity thresholds become configurable, or remain conservative backend constants until operators have real production feedback?
- Should global share observability be promoted to a system dashboard card, or stay per-vault only unless a clear global operator question emerges?

## Phase 11

- Should the nav severity poll interval stay at 15 seconds, or should it become configurable with the rest of dashboard polling?
- Should dashboard overview severity helper tests wait for a frontend unit test runner, or should a tiny local script under `.codex/scripts` cover pure helper checks until then?

## Phase 12

- Should Phase 13 move the dashboard card catalog to the backend so persisted layout preferences can validate supported sizes/variants authoritatively?
- Should browser-local dashboard layouts remain as a fallback after server-side persistence lands, or should server preferences fully replace them?
- Should drag/drop in Phase 13 keep the same finite size set or introduce section-aware constraints for cards that are too dense in `1x1` mode?

## Phase 13

- Should the backend become the authoritative dashboard card catalog so preference validation can reject unsupported card IDs/sizes before persistence?
- Should future custom named dashboard pages reuse `dashboard_preferences.preference_key`, or should they get a separate table with sharing/copy semantics?
- Should drag/drop gain full keyboard drag semantics, or are the retained Up/Down controls sufficient for the next accessibility pass?

## Phase 14

- Should the web package add a small route/middleware unit test runner so auth proxy classification and middleware redirects can be tested directly instead of only through typecheck/lint?
- Should `/api/auth/session` eventually expose a diagnostic 503 mode for admin-only health checks, or should it continue returning 401 for upstream auth unavailability to keep middleware behavior fail-closed and simple?
