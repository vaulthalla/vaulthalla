# Web client (`web/`)

- Next.js 16 app router + React 19 + Zustand 5, TypeScript strict. `output: 'standalone'`.
- Toolchain: Node `24.13` (`web/.nvmrc`), `packageManager: pnpm@11.0.8`. `web/package.json` also carries
  `version` (release-managed, kept in sync with `VERSION`).
- Scripts: `dev` (turbopack), `dev:e2e`, `build`, `start`, `typecheck` (`tsc --noEmit`), `lint`
  (`eslint src`, not `next lint`), `test` (= typecheck + lint; **there is no unit test runner**),
  `test:e2e` / `test:e2e:s3-gateway` (Playwright, `web/tests/e2e`, `playwright.config.ts`).

## Layout

- `src/app/(auth)`: login. `src/app/(app)/(admin)`: admin area (dashboards, operator-email, users,
  roles, S3 gateway…). `src/app/(app)/(fs)`: filesystem UI. `src/app/share/[token]`: public shares.
  `src/app/api/auth/session`, `src/app/api/runtime/config`: server routes.
- `src/stores/*`: ws-driven Zustand stores. `useWebSocket.ts` is the transport, `fsStore.ts` holds filesystem state,
  `vaultShareStore.ts` holds share bootstrap only, `statsStore`.
- `src/models/*`: typed payloads. The `WebSocketCommandMap` typing must be extended for every new ws command.
- Path aliases: `@/* → src/*`, `@/icons/*` → `public/icons`.

## Wiring and env

- `middleware.ts` gates auth via `/api/auth/session` on `VAULTHALLA_WEB_INTERNAL_ORIGIN` (prod fallback
  `127.0.0.1:36968`). The session route proxies to `VAULTHALLA_AUTH_ORIGIN` → `VAULTHALLA_PREVIEW_ORIGIN` →
  `http://127.0.0.1:36970`.
- The WS URL is `ws(s)://<location.host>/ws` unless `NEXT_PUBLIC_VAULTHALLA_WS_ORIGIN` is set. The app expects a reverse
  proxy that routes `/ws` → 36969 and `/preview|/download|/upload` → 36970 (nginx in prod, `Caddyfile` in dev).
- `next.config.ts`: SVGR loader (webpack + `turbopack.rules`), `images.localPatterns /preview**`,
  `allowedDevOrigins: ['vh.home.arpa']`.

## Private icons (build gotcha)

FontAwesome Pro icons aren't in git (`/web/public/icons/fa/` is ignored). `.github/actions/sync_web_icons`
copies only the icons the code uses from `$VAULTHALLA_WEB_ICON_SRC` (default `~/vaulthalla-web-icons`).
A fresh checkout's `pnpm build` fails without them.

## Packaging

`web/bin/install_web.sh` (source installs) and `web/bin/build_package.sh` stage `.next/standalone` + `.next/static` +
`public` into `/usr/share/vaulthalla-web`, which runs as `vaulthalla-web.service` (`node server.js`).
The legacy standalone `web/debian/` package is gone.
