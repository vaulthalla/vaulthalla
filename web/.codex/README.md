# Codex Workspace Toolkit

This folder contains repo-local conventions and helper tooling to make Codex + human collaboration faster and more consistent.

## Quick Start

From repo root:

```bash
bash .codex/scripts/doctor.sh
bash .codex/scripts/verify.sh
# optional strict lint gate:
VERIFY_STRICT_LINT=1 bash .codex/scripts/verify.sh
```

## Layout

- `.codex/config/project.env`: single source of truth for project commands.
- `.codex/scripts/doctor.sh`: environment sanity check (node, pnpm, git, deps).
- `.codex/scripts/verify.sh`: full local validation pass (typecheck + eslint).
- `.codex/scripts/changed.sh`: lint/typecheck helpers for changed files.
- `.codex/scripts/snapshot.sh`: writes a task handoff snapshot to `.codex/context/`.
- `.codex/checklists/`: delivery checklists for feature/bugfix/review flows.
- `.codex/prompts/`: reusable high-signal prompts for scoping and handoff.
- `.codex/templates/`: markdown templates for plans and PR summaries.

## Typical Workflow

1. Run `doctor` when opening a new session.
2. Implement changes.
3. Run `changed` during iteration.
4. Run `verify` before handoff.
5. Run `snapshot` when handing work to another person/agent.

## Notes

- `next lint` is not used here; lint runs directly through `eslint` to avoid Next CLI mismatches.
- Scripts are intentionally POSIX-ish `bash` and avoid external dependencies.
