# Web console overhaul: dogfood results (2026-10-03/04)

Branch `web-overhaul`. Plan: `.claude/scratch/web_overhaul/goal.md` (Phase A overhaul, Phase B dogfood). Board: epics
#155 (overhaul, sub-issues #137–#154) and #156 (dogfood).

## Where it ran

- **Local dev VM**, source-installed daemon **1.8.0** (MST PostgreSQL), nginx with a self-signed cert on
  `https://localhost`, the new console served from the production build (`pnpm build` + `next start` on the
  console port 36968; the packaged `vaulthalla-web.service` stopped for the run).
- The new daemon code (this branch's core changes) was proven by the DB-backed unit suite only: wiping the dev
  install to run *released 1.8.0 → candidate .deb* was refused by the session's permission check and needs the
  maintainer's go-ahead. A candidate package builds clean with `make deb` (`vlr prepare` + `build-deb` +
  `validate-artifacts`, nothing published).
- **Not run yet:** the lab (`vh-storage`) — real apt only, after a release; Firefox / iOS Safari passes.

## Suites (all in `web/tests/e2e`, run against `https://localhost`, production build)

| Suite | Result |
|---|---|
| `console.spec.ts` route sweep (17 routes: no page/console errors, no endless loading, no denial for a super admin) | pass |
| redirects from every old console URL | pass |
| file lifecycle: folder, upload incl. `naïve résumé (1) #hash %20.txt` and a dotfile, F2 rename, preflighted download, copy/move, URL path + reload/back, confirmed delete | pass |
| multi-item drop (5 files through Chrome's native drag events) | pass |
| logout hygiene (no `vaulthalla-*` storage, `/files` → login) | pass |
| share links: recipient browses a folder link; dropbox link is upload-only and the upload arrives | pass, **cleanup fails on 1.8.0**: deleting the dropbox folder hits the share_upload FK bug fixed by migration 101 |
| ws bootstrap ≤ 20 KB on `/files` + no stats polling after leaving `/health` | **fails on 1.8.0** (23 KB: the 1.8.0 session payload carries every permission description); fixed in core on this branch (slim session user), needs the new daemon to pass |
| `personas.spec.ts`: unprivileged user, plain `admin` role, inactive user | pass |
| `s3-gateway.spec.ts` (13 tests) | pass |
| `pnpm budgets` (34 routes, hard CI gate) | pass |
| core unit suite (DB-backed) | 641 run, 639 pass, 2 skipped (live SES) — before the core-fixes branch |

## Bugs found while dogfooding (fixed on this branch)

- Selecting a row inserted the selection bar above the list and pushed rows down, so a double-click opened the
  folder above (fs). Fixed: the bar overlays the toolbar.
- Native checkbox glyph in a Tailwind class broke `tailwind-merge`; checked boxes looked empty. Fixed (CSS class).
- Two `router.replace` calls in a row dropped the second in production builds, so S3 gateway / cost tabs stopped
  switching. Fixed: `replaceQuery()` (history.replaceState).
- Share-dialog role templates filtered on a `type` field vault roles don't carry; nothing was selectable. Fixed.
- Folder that received a share upload couldn't be deleted (raw FK violation shown to the user). Fixed in core
  (migration 101).
- Session payload ~13 KB of permission descriptions on every page load. Fixed in core (slim session user).

## Bugs found and filed (open)

#157 timestamps off by the UTC offset (fixed on this branch: UTC sessions + migration 100) ·
#158 directory counters not updated on move · #159 overview severities report unknown as healthy ·
#160 stats payload problems (24 h trends, FS cache capacity, hrefs, money strings) · #161 every vault reports the
default vault's size · #162 vault delete leaves backing data (decision) · #163 `password_changed_at` never stored ·
#164 settings the daemon never reads (decision) · #165 gateway ws handlers overload `id` · #166 built-in `admin`
role can't see Health/stats (decision).
(#158/#159/#161/#163 are being fixed on `web-overhaul-core-fixes` at the time of writing.)

## How to re-run

```bash
bash tools/e2e/provision_e2e_user.sh                                  # local dev DB only
cd web && pnpm build && PORT=36968 node_modules/.bin/next start --hostname 127.0.0.1 --port 36968   # behind nginx
VAULTHALLA_E2E_BASE_URL=https://localhost VAULTHALLA_E2E_NO_WEB_SERVER=1 \
  pnpm exec playwright test tests/e2e/console.spec.ts tests/e2e/personas.spec.ts tests/e2e/s3-gateway.spec.ts
```

Use a production build: React StrictMode doubles every request under `next dev`, which breaks the ws budget test.
