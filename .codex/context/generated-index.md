# Generated Monorepo Index

- Generated: 2026-04-19
- Source: repository scan via `find` in Codex session

## Top-Level Directories

```text
.github
bin
build
core
debian
deploy
tools
web
```

## Core Protocol Files (selected)

```text
core/include/protocols/http/Server.hpp
core/include/protocols/shell/Server.hpp
core/include/protocols/ws/Server.hpp
core/src/protocols/ProtocolService.cpp
core/src/protocols/http/Server.cpp
core/src/protocols/shell/Router.cpp
core/src/protocols/shell/Server.cpp
core/src/protocols/ws/Server.cpp
```

## Web App Surface (selected)

```text
web/src/app/(app)/(admin)/*
web/src/app/(app)/(fs)/*
web/src/app/(auth)/login/page.tsx
web/src/app/api/auth/session/route.ts
web/src/components/*
web/src/stores/useWebSocket.ts
web/src/util/getUrl.ts
```

## Deploy + Packaging Surface (selected)

```text
deploy/config/config.yaml
deploy/psql/*.sql
deploy/systemd/vaulthalla.service.in
deploy/systemd/vaulthalla-cli.service.in
deploy/systemd/vaulthalla-cli.socket
debian/control
debian/install
debian/postinst
debian/changelog
```

## Release Tooling Surface

```text
tools/release/cli.py
tools/release/version/*
tools/release/changelog/*
tools/release/debug/release_context.py
```
