# Web Client Map (`web/`)

## Stack Snapshot

`web/package.json`:

```json
{
  "name": "vaulthalla-web",
  "private": true,
  "scripts": {
    "dev": "next dev --turbopack",
    "build": "next build",
    "test": "pnpm run typecheck && pnpm run lint"
  },
  "dependencies": {
    "next": "^16.2.4",
    "react": "^19.2.5",
    "react-dom": "^19.2.5",
    "zustand": "^5.0.12"
  },
  "packageManager": "pnpm@10.30.3"
}
```

Toolchain pin:

- Node version: `web/.nvmrc` => `24.13`

## App Structure

Key directory layout:

- `web/src/app/(auth)` login/auth shell
- `web/src/app/(app)/(admin)` admin area
- `web/src/app/(app)/(fs)` filesystem UI
- `web/src/components/*` feature/component surfaces
- `web/src/stores/*` websocket-driven state stores (Zustand)

## Runtime Wiring

### Middleware auth gate

`web/middleware.ts` calls `GET /api/auth/session`, and redirects to `/login` when unauthorized.

### Session proxy route

`web/src/app/api/auth/session/route.ts` proxies cookies to:

- `${process.env.NEXT_PUBLIC_SERVER_ADDR}/auth/session`

### Websocket transport

`web/src/stores/useWebSocket.ts` handles:

- socket lifecycle and reconnects
- pending request map by `requestId`
- command send/response routing
- auth token injection

Websocket URL builder (`web/src/util/getUrl.ts`):

```ts
const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:'
return `${scheme}//${location.host}/ws`
```

So websocket traffic is expected behind a reverse proxy route `/ws`.

## Next Config

`web/next.config.ts`:

- `output: 'standalone'` for deployable Node bundle
- SVGR loader config for SVG import support
- `allowedDevOrigins: ['vh.home.arpa']`

## TypeScript

`web/tsconfig.json` is strict (`"strict": true`) and defines path aliases:

- `@/* -> ./src/*`
- `@/icons/*` and related icon aliases into `public/icons`

## Packaging Notes

- `web/bin/build_package.sh` packages `.next/standalone`, `.next/static`, and `public` into `pkgroot/usr/share/vaulthalla-web`.
- `web/debian/` still contains legacy standalone web package scaffolding (`vaulthalla-web`) and should be treated as historical unless explicitly revived.
