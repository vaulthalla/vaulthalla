# Architecture Map

This is the contributor-facing map of the repo. It is not a full internal design document. Its job is to help you answer five questions quickly:

1. what subsystem am I touching
2. where does that code live
3. how sharp is it
4. what kind of contribution is appropriate
5. what should I validate before opening a PR

## Top-Level Repo Map

| Area | What it contains |
| --- | --- |
| `core/` | C++ daemon, CLI transport, FUSE service, runtime manager, auth, RBAC, crypto, sync, storage, previews, tests, usage/manpage generation |
| `web/` | Next.js admin and filesystem client, websocket stores, auth/session proxy route |
| `deploy/` | Runtime config, PostgreSQL schema SQL, systemd units, nginx template, lifecycle utility |
| `debian/` | Debian package metadata and maintainer scripts |
| `bin/` | Dev and operator install/uninstall/test helpers |
| `tools/release/` | Versioning, changelog generation, packaging, publication, and contract tests |
| `.github/` | CI workflows and composite actions |

## Subsystem Map

| Subsystem | What it does | Main paths | Contribution fit | Likely validation |
| --- | --- | --- | --- | --- |
| Core runtime manager | Starts and supervises runtime services such as sync, FUSE, protocol services, log rotation, DB janitor, and shell server | `core/main/main.cpp`, `core/src/runtime/*`, `core/src/protocols/ProtocolService.cpp` | Maintainer-guided | Linux build, targeted runtime checks, integration coverage when behavior changes |
| FUSE and filesystem behavior | Mounts the filesystem, dispatches FUSE requests, and enforces filesystem semantics | `core/src/fuse/*`, `core/include/fuse/*`, `core/src/fs/*`, `core/include/fs/*` | Maintainer-guided | Linux-only runtime validation, integration tests, manual mount/unmount behavior checks |
| CLI and local shell protocol | Implements `vh` and `vaulthalla`, talks to `/run/vaulthalla/cli.sock`, and shares help/manpage generation | `core/main/cli.cpp`, `core/src/protocols/shell/*`, `core/usage/*`, `deploy/systemd/vaulthalla-cli.socket` | Open for help text, coordinate for behavior, maintainer-guided for lifecycle/protocol changes | Core build, CLI usage review, integration tests for command behavior |
| Web/admin client | Next.js app for admin and filesystem flows, backed by websocket requests and an auth/session proxy route | `web/src/app/*`, `web/src/components/*`, `web/src/stores/*`, `web/src/app/api/auth/session/route.ts` | Open for small polish, coordinate for flow changes | `cd web && pnpm test`, manual local UI checks when possible |
| Database and query layer | Owns schema files, prepared statements, and DB access for auth, FS metadata, RBAC, sync, and vault state | `deploy/psql/*`, `core/src/db/*`, `core/include/db/*` | Maintainer-guided | PostgreSQL-backed validation, integration tests, schema review |
| Storage providers | Manages storage engine abstractions and S3-compatible provider behavior | `core/src/storage/*`, `core/include/storage/*`, `core/src/vault/model/*`, `core/include/vault/*` | Coordinate before implementing | Linux/runtime validation, storage-provider-specific checks, honest test notes if remote credentials were not available |
| Preview and thumbnail pipeline | Generates previews and thumbnails for supported content types and feeds the filesystem UI | `core/src/preview/*`, `core/include/preview/*`, `web/src/components/fs/FilePreviewModal.tsx`, `web/src/components/fs/Thumb.tsx` | Coordinate before implementing | Targeted file-format testing, UI preview checks, core build |
| Sync engine | Plans and executes upload, download, delete, conflict, and key-rotation work | `core/src/sync/*`, `core/include/sync/*` | Maintainer-guided | Integration-style validation, repro steps, manual data-safety checks |
| RBAC and permission model | Defines admin and vault permissions, glob/path policy logic, role templates, and permission resolution | `core/src/rbac/*`, `core/include/rbac/*`, `deploy/psql/060_acl.sql` | Security-sensitive | Maintainer approval first, focused tests, threat-aware review |
| Auth, session, and secret handling | Manages token issuance, refresh/session validation, secret storage, and auth-related protocol behavior | `core/src/auth/*`, `core/include/auth/*`, `core/src/crypto/*`, `core/include/crypto/*`, `web/src/stores/useWebSocket.ts` | Security-sensitive | Maintainer approval first, focused tests, no hand-wavy validation |
| Packaging, systemd, and lifecycle | Defines package payload, install/remove/purge scripts, systemd units, and privileged host setup flows | `debian/*`, `deploy/systemd/*`, `deploy/lifecycle/main.py`, `bin/setup/*`, `bin/teardown/*` | Coordinate before implementing | Package dry runs, lifecycle tests, clean-host install/upgrade/remove/purge checks |
| Release tooling | Manages version sync, changelog generation, packaging orchestration, publication, and contract tests | `tools/release/*`, `.github/actions/package/action.yml`, `.github/workflows/release.yml` | Maintainer-guided | `python3 -m tools.release check`, release-tooling test suite, dry-run packaging |
| Tests and harnesses | Unit, integration, lifecycle, and release contract coverage | `core/tests/*`, `deploy/lifecycle/tests/*`, `tools/release/tests/*` | Open for scoped work | Run the relevant test surface and avoid unrelated churn |

## Subsystem Notes

### Core runtime

`core/src/runtime/Manager.cpp` is where service orchestration comes together. If your change affects startup order, restart behavior, shutdown behavior, or service wiring, treat it as maintainer-guided work.

### FUSE and filesystem semantics

`core/src/fuse/Service.cpp` mounts the filesystem with `allow_other` and `auto_unmount`. That is not casual plumbing. Filesystem behavior changes can affect user data, trust boundaries, and operator expectations.

### CLI and shell protocol

`core/main/cli.cpp` talks to the local control socket at `/run/vaulthalla/cli.sock`. Help text and usage improvements are easy contributions. Protocol or privileged lifecycle command changes are not.

### Web/admin surface

The web client is a real contributor surface, but it is still wired into auth and websocket behavior. Small UX work is fair game. New flows, new command surfaces, and auth/session changes should start with discussion.

One practical caveat: the CI web build syncs private icon assets from `~/vaulthalla-web-icons` in `.github/actions/build_web/action.yml`. If you are touching web UI and hit build failures related to missing private icons, coordinate with the maintainer rather than stubbing fake assets into the repo.

### Database and schema

`deploy/psql/000_schema.sql` through `deploy/psql/060_acl.sql` define the installed schema surface. Schema work is sharp because it crosses packaging, bootstrap, runtime queries, retention, and upgrade safety.

### Packaging and lifecycle

The Debian maintainer scripts in `debian/postinst`, `debian/prerm`, and `debian/postrm` are the lifecycle source of truth for install, upgrade, remove, and purge. The `bin/` scripts are helpful local wrappers, but they are not the Debian contract.

### Release tooling

`tools/release/` is not just a convenience folder. It owns version alignment, changelog production, dry-run packaging, publication policy, and release artifact validation. Treat it like a product surface, not a scratchpad.

## Read By Interest

If you are working on:

- docs and onboarding: start with [Contribution Boundaries](./contribution-boundaries.md) and [Validation Guide](./validation-guide.md)
- web/admin polish: read `web/src/app/*`, `web/src/components/*`, and `web/src/stores/*`
- CLI help and examples: read `core/usage/*` and `core/main/cli.cpp`
- packaging: read [Packaging and Release](./packaging-and-release.md), then `debian/*` and `deploy/systemd/*`
- security-sensitive work: read [Security-Sensitive Work](./security-sensitive-work.md) first
