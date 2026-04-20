# Codex Workspace Toolkit (Monorepo)

This folder is the shared Codex operator toolkit for the entire `vaulthalla` monorepo:

- `core/`: C++23 backend (runtime manager, FUSE daemon, websocket/http/shell protocols, usage/manpage generation, integration tests)
- `web/`: Next.js web client
- `deploy/`: config templates, postgres schema SQL, systemd unit templates
- `debian/`: Debian packaging for the single `vaulthalla` package
- `tools/release/`: semantic version + release/changelog automation
- `bin/`: install/uninstall/test automation scripts

## Quick Start

From repository root:

```bash
bash .codex/scripts/doctor.sh
bash .codex/scripts/index.sh
bash .codex/scripts/changed.sh all
bash .codex/scripts/verify.sh all
# optional strict lint gate:
VERIFY_STRICT_LINT=1 bash .codex/scripts/verify.sh web
```

## Layout

- `.codex/config/project.env`: command and path defaults for monorepo checks.
- `.codex/scripts/doctor.sh`: environment + toolchain sanity checks.
- `.codex/scripts/index.sh`: refreshes `.codex/context/generated-index.md`.
- `.codex/scripts/verify.sh`: profile-based verification (`web`, `release`, `all`).
- `.codex/scripts/changed.sh`: change-aware checks for web/release surfaces.
- `.codex/scripts/snapshot.sh`: writes task handoff snapshot to `.codex/context/`.
- `.codex/context/`: durable architecture and workflow context extracted from source files.
- `.codex/checklists/`: delivery checklists for feature/bugfix/review flows.
- `.codex/prompts/`: reusable high-signal prompts for scoping and handoff.
- `.codex/templates/`: markdown templates for plans and PR summaries.

## Typical Workflow

1. Run `doctor` and `index` when starting.
2. Read `.codex/context/index.md` and relevant subsystem docs.
3. Implement changes.
4. Run `changed` during iteration.
5. Run `verify` for the affected surface(s) before handoff.
6. Run `snapshot` for clean handoff context.

## Notes

- Web linting runs directly through `eslint` (not `next lint`) to avoid CLI mismatch noise.
- `tools/release` treats top-level `VERSION` as source of truth.
- Scripts are intentionally `bash` with minimal external dependencies.
