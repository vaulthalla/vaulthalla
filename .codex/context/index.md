# Vaulthalla Context Index

This folder is the Codex memory map for the monorepo.

## Read Order

1. `repo-overview.md` for top-level ownership map.
2. `core-backend.md` for C++ runtime/protocol architecture.
3. `protocol-map.md` for shell/ws/http/FUSE request flow.
4. `web-client.md` for Next.js app wiring and websocket surface.
5. `deploy-packaging.md` for systemd, PostgreSQL bootstrap, Debian payload.
6. `debian-packaging-lifecycle.md` for maintainer scripts, prompts, install/remove/purge contract.
7. `release-tooling.md` for version/changelog/release/publication automation.
8. `testing-ci.md` for CI and local verification paths.

## Fast Reality Checks

- Canonical version file: `VERSION`
- Core build manifest: `core/meson.build`
- Web package manifest: `web/package.json`
- Runtime config: `deploy/config/config.yaml`
- Debian package metadata: `debian/control`, `debian/install`, `debian/changelog`
- Release CLI: `python3 -m tools.release`

## Generated Index

Run:

```bash
bash .codex/scripts/index.sh
```

This refreshes `generated-index.md` with current file/dir maps.
