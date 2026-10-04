# Web client (`web/`)

- Next.js 16 app router + React 19, TypeScript strict, Tailwind 4. `output: 'standalone'`.
- Toolchain: Node `24.13` (`web/.nvmrc`), `packageManager: pnpm@11.0.8`. `web/package.json` also carries `version`
  (release-managed, kept in sync with `VERSION`).
- Scripts: `dev` (turbopack), `dev:e2e`, `build`, `start`, `typecheck` (`tsc --noEmit`), `lint` (`eslint src`),
  `colors` (design-token guard), `test` (= typecheck + lint + colors; **there is no unit test runner**),
  `budgets` (first-load JS gate, needs a `build`), `test:e2e` (Playwright, `web/tests/e2e`).

## Architecture (since the 2026-10 overhaul)

- **Transport:** `src/lib/ws/client.ts` — one `WsClient` class for the console socket (`/ws`) and the anonymous
  share socket (`/ws/share`): connect deadline, exponential backoff with jitter, reconnect on `online`/visible,
  per-command timeouts (`COMMAND_TIMEOUTS`; a timeout means "no answer yet", never "failed"), stale-socket guards,
  typed `WsError` kinds (`unauthorized | denied | not_found | invalid | conflict | needs_confirmation | error |
  timeout | disconnected | aborted`) from the envelope `status` + `data.code`.
- **Session:** `src/lib/session.ts`. The access token lives **in memory only**; the HttpOnly `refresh` cookie set at
  the ws handshake re-issues it (`auth.refresh`) on every page load. Single-flight refresh; an `UNAUTHORIZED`
  response refreshes and retries once (the router never ran it). Logout sends `auth.logout`, runs every
  `onSessionReset` handler, clears legacy `vaulthalla-*` keys and hard-navigates to `/login`.
- **Server data:** TanStack Query over ws (`src/lib/query.ts`: `useWs`, `useWsMutation`, `invalidate`, `fetchWs`).
  Polling = `refetchInterval` (deduped, ref-counted, paused in hidden tabs). No Zustand stores for server data;
  Zustand only for client state (transfers, UI prefs, confirm/palette state).
- **Permissions (UI only):** `src/lib/permissions.ts` `useCan({admin|permission|anyOf|prefix})`, mirroring core
  `User::isAdmin()`. Nav items declare `requires`; pages render a typed denied state.
- **Design system:** tokens in `src/app/globals.css` (dark only; one cyan accent; `surface-1/2/3`, `line`,
  `fg/-muted/-subtle/-faint`, status `ok|info|warn|danger|unknown`; utilities `glass`, `glass-strong`, `panel`,
  `tabular`, `skeleton`). Primitives in `src/components/ui/*` (Button, IconButton, Field/Input/Select/Textarea,
  Choice (native Checkbox/Switch), Dialog/Sheet, `confirm()`, Menu (Dropdown + pointer-anchored context menu, Radix
  lazy-loaded on first interaction), Popover (lazy), CSS-only Tooltip, Tabs/LinkTabs/Segmented, Badge, Panel/
  PageHeader, Empty/Error/QueryState, Stat tiles/Meter/Sparkline/SegmentBar, DataTable, `notify`, `icons.ts`).
  `bin/check-colors.mjs` fails on raw palette/arbitrary colors outside `components/ui`. `/dev/ui` (development
  builds only) renders every primitive.
- **Shell:** `src/components/shell/*` — permission-filtered rail (collapsible), top bar (⌘K command palette, health
  dot from `stats.dashboard.severity`, transfers, cost-alerts bell, user menu), session gate (reconnecting state, never
  an endless spinner), initial-password warning (`data-testid="initial-password-warning"`).
- **Cost-alerts bell** (`features/cost/NotificationsBell.tsx`, super admins only): `pricing.notifications.list`
  `{limit: 8, include_acknowledged: false}` every 60 s (paused in hidden tabs). The badge count (capped "9+") and tone
  come from the response's `summary` (`open_count`, `worst_severity`), which core computes over every open alert in
  scope, never from the shown rows (#172); "View all N alerts" links to `/cost#budget-alerts` when more are open. The
  cost page's alert list (`useNotifications`, limit 50, optional acknowledged rows) is separate.
- **Features:** `src/features/<area>/*`; route files in `src/app/**/page.tsx` are thin and render a feature
  component. Areas: `files` (FileBrowser over an `FsSource` adapter — vault or share — with capability-driven UI,
  URL-addressed paths, virtualized list/grid, transfer manager), `shares`, `share` (anonymous recipient page),
  `health`, `vaults`, `access`, `account`, `credentials`, `cost`, `gateway`, `notifications`, `settings`, `auth`.

## Routes

`/login`, `/files/[vaultId]/[...path]`, `/shares`, `/vaults` (+ `/new`, `/[id]` tabs: overview, access, shares,
sync, gateway, settings), `/users` (+ `/new`, `/[name]`), `/groups`, `/roles` (+ `/new?type=`, `/[type]/[id]`),
`/credentials` (+ `/new`, `/[id]`), `/cost`, `/s3-gateway`, `/health` (+ runtime, filesystem, storage, activity),
`/notifications`, `/settings`, `/account`, `/share/[token]/[...path]` (public, no shell). Old URLs (`/fs`,
`/dashboard/*`, `/api-keys/*`, `/pricing-budget`, `/operator-email`, `/users/add`, `/vaults/:id/edit|assign`,
`/roles/admin|vault/*`) redirect (`next.config.ts`).

## Performance gate

`pnpm budgets` (`bin/check-budgets.mjs` + `perf-budgets.json`) measures first-load JS per route (gzip; root chunks +
layout/page entry chunks; dynamic imports and legacy polyfills excluded) from the production build and fails over
budget. CI runs it after `pnpm build` (hard gate); `tools/dev/verify.sh web` runs it with `VERIFY_WEB_BUILD=1`.
Next 16 + React 19 alone are ~143 KB. Keep dialogs/editors/charts/menus behind `next/dynamic` or lazy primitives.

## Wiring and env

- `middleware.ts` makes no upstream call: public paths (`/login`, `/share`, static files) pass; any other path
  needs a non-empty `refresh` cookie or it redirects to `/login?next=<path+query>`. Whether the cookie is still valid
  is decided by the websocket session gate (`SessionGate` → `auth.refresh`): a refusal sends the user to
  `/login?next=…`, while an unreachable daemon shows the reconnect state, never the login page. Pages carry no data
  (everything loads over the socket after the gate), so the presence check is all the middleware needs. Until #171
  it verified the cookie against the daemon's `GET /auth/session` on each document load, which cost a ~0.57 s Argon2
  verify per page; that path (`src/lib/server/authCheck.ts`, `/api/auth/session`) is gone. The daemon's
  `GET /auth/session` itself still exists. Guard: console e2e "middleware only checks for a refresh cookie".
- The WS URL is `ws(s)://<location.host>/ws` unless `NEXT_PUBLIC_VAULTHALLA_WS_ORIGIN` is set. The app expects a reverse
  proxy that routes `/ws` → 36969 and `/preview|/download|/upload` → 36970 (nginx in prod, `Caddyfile` in dev).
  HTTP uploads: `POST /upload/session[?share=1]` → `PUT /upload/<id>/files/<fileId>` → `POST /upload/<id>/finish`
  (`DELETE` on failure); the client splits large drops into several sessions. Downloads are preflighted with a
  `fetch` before handing the URL to the browser.
- `next.config.ts`: SVGR loader (webpack + `turbopack.rules`), `images.localPatterns /preview**`, redirects,
  `devIndicators: false`. `package.json` `sideEffects: ["**/*.css"]`.

## Private icons (build gotcha)

FontAwesome Pro icons aren't in git (`/web/public/icons/fa/` is ignored). `web/bin/sync_private_icons.sh` (CI:
`.github/actions/sync_web_icons`) copies only the icons the code imports (`@/fa-*/…svg`, mostly via
`src/components/ui/icons.ts`) from `$VAULTHALLA_WEB_ICON_SRC` (default `~/vaulthalla-web-icons`). A fresh checkout's
`pnpm build` fails without them; any new icon must exist in that store.

## Testing

`tests/e2e/console.spec.ts` is the console dogfood suite (route sweep, redirects, ws budget and poller shutdown,
file lifecycle, multi-item drop, shares incl. upload-only dropbox, logout hygiene); run it against a reverse-proxied
install (`VAULTHALLA_E2E_BASE_URL=https://localhost VAULTHALLA_E2E_NO_WEB_SERVER=1`). `lab-first-run.spec.ts` is the
packaged-install proof; `s3-gateway.spec.ts` drives the gateway page by `data-testid`, records the budget policies it
saves (from the `*.budget.policy.upsert` responses on `/ws`) and in `afterAll` acknowledges their "policy <id> was
saved|disabled" alerts on `/cost` before deleting the vaults it created.

## Packaging

`web/bin/install_web.sh` (source installs) and `web/bin/build_package.sh` stage `.next/standalone` + `.next/static` +
`public` into `/usr/share/vaulthalla-web`, which runs as `vaulthalla-web.service` (`node server.js`).
