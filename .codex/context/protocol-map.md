# Protocol + Command Flow Map

## 1) Shell CLI Flow (`vh` / `vaulthalla`)

1. User runs `/usr/bin/vh` or `/usr/bin/vaulthalla` (symlink to `vaulthalla-cli`).
2. `core/main/cli.cpp` normalizes args and connects to `/run/vaulthalla/cli.sock`.
3. CLI sends JSON frame with `cmd`, `args`, `line`, `interactive`.
4. `core/src/protocols/shell/Server.cpp` authenticates peer UID/group and dispatches request.
5. `core/src/protocols/shell/Router.cpp` tokenizes/parses command line and executes command handler.
6. Result/output frames stream back to CLI client.

Usage/help definitions live in `core/usage/*` and are shared with manpage generation (`vh_usage`).

## 2) WebSocket Flow (`/ws`)

1. Web client creates socket from `web/src/util/getUrl.ts` (`ws(s)://<host>/ws`).
2. Store lifecycle (`web/src/stores/useWebSocket.ts`) manages reconnect + pending request handlers.
3. Backend websocket server comes from `core/src/protocols/ws/Server.cpp`.
4. Handler registration is in `core/src/protocols/ws/Handler.cpp` + `handler/*`.
5. Session lifecycle manager in `core/src/protocols/ws/ConnectionLifecycleManager.cpp`.

## 3) HTTP Preview/Auth Proxy Flow

1. Browser middleware (`web/middleware.ts`) calls `/api/auth/session`.
2. Next route (`web/src/app/api/auth/session/route.ts`) proxies to backend preview/auth endpoint using `NEXT_PUBLIC_SERVER_ADDR`.
3. Backend preview server is `core/src/protocols/http/Server.cpp`.

## 4) Runtime Service Orchestration

`core/src/runtime/Manager.cpp` starts/stops/watchdogs:

- protocol service
- shell server
- fuse service
- sync controller
- lifecycle/log/db janitor services

## 5) FUSE Filesystem Flow

`core/src/fuse/Service.cpp`:

- validates mountpoint state
- mounts low-level FUSE session
- dispatches requests to thread pool tasks
- unmounts and destroys session on shutdown
