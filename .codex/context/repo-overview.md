# Monorepo Overview

## Top-Level Responsibilities

- `core/`: backend daemon + CLI transport + usage/manpage generator + integration harness.
- `web/`: Next.js app router client using websocket commands + preview/auth middleware.
- `deploy/`: runtime config, SQL schema/migrations, systemd templates, env example.
- `debian/`: Debian packaging for single package `vaulthalla`.
- `tools/release/`: version management, deterministic changelog pipeline, AI draft stages, Debian packaging orchestration.
- `bin/`: install, uninstall, test install flows and system setup scripts.
- `.github/`: composite actions and workflows for core/web build+test and release package.

## Versioning Source of Truth

`VERSION` is canonical. Managed files that must stay aligned:

- `meson.build`
- `web/package.json`
- `debian/changelog` (upstream version + revision)

Release automation entrypoints:

```bash
python3 -m tools.release check
python3 -m tools.release sync
python3 -m tools.release set-version X.Y.Z
python3 -m tools.release bump patch
python3 -m tools.release changelog draft --format raw
python3 -m tools.release changelog payload
python3 -m tools.release changelog ai-draft --provider openai-compatible --base-url http://localhost:8888/v1
python3 -m tools.release build-deb --dry-run
```

## Build + Install Entry Surface

- `Makefile` wraps install/test/deb/release flows.
- `bin/install.sh` orchestrates dependency/user/dir/core/web install and systemd wiring.
- `bin/uninstall.sh` orchestrates unmount/systemd removal and optional DB/user/deps purge.
- `web/bin/install_web.sh` owns web dependency install/build + runtime payload install at `/usr/share/vaulthalla-web`.
- `web/bin/teardown_web.sh` removes the installed web runtime payload.
- `bin/setup/install_dirs.sh` creates symlinks:
  - `/usr/bin/vaulthalla -> /usr/bin/vaulthalla-cli`
  - `/usr/bin/vh -> /usr/bin/vaulthalla-cli`

## Core Runtime Services (high-level)

`core/src/runtime/Manager.cpp` registers:

- `SyncController`
- `FUSE`
- `ProtocolService` (websocket + preview HTTP)
- `ConnectionLifecycleManager`
- `LogRotationService`
- `DBJanitor`
- `ShellServer` (when not in test mode)

## Important Paths (from Meson-generated `paths.h`)

- Config: `/etc/vaulthalla/config.yaml`
- Runtime: `/run/vaulthalla`
- Mount: `/mnt/vaulthalla`
- State: `/var/lib/vaulthalla`
- Logs: `/var/log/vaulthalla`
- SQL assets: `/usr/share/vaulthalla/psql` (installed)
