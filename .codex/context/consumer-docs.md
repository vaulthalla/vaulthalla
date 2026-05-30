# Consumer Documentation Context

## Documentation Intent

`docs/` is the operator and end-user documentation source. It is about using Vaulthalla, not explaining implementation internals. Prioritize installation, setup, CLI usage, web console workflows, vault operation, sync, encryption, backup, export, cost control, sharing, and troubleshooting.

Contributor-facing docs can live under `docs/contributors/`, but the primary navigation and new coverage should serve production operators first.

## Payload Markdown Docs Standards

These docs follow the installed `payload-markdown-docs` and `payload-markdown` skills:

- Human-authored docs live under `docs/`.
- Every doc page should have YAML frontmatter with supported fields only.
- Common fields: `title`, `navTitle`, `description`, `order`, `status`, and `tags`.
- Use `status: published` for complete operator pages and `status: draft` only when intentionally incomplete.
- Use root-relative route links such as `/vaults/sync`, not `./sync.md`.
- Do not commit generated AI export files such as root `llms.txt`, `llms-full.txt`, `index.ai.yml`, or `index.ai.yaml`.
- Long pages should include a compact table of contents after the introduction:

```md
:::toc[On this page]{depth="3" theme="compact"}
:::
```

Supported directives currently used include `toc`, `cards`, `card`, `callout`, and `steps`.

## Current Consumer Coverage

Core operator entry points:

- `docs/index.md`
- `docs/getting-started/installation.md`
- `docs/getting-started/first-run.md`
- `docs/getting-started/web-console.md`
- `docs/web-console/index.md`

CLI and command coverage:

- `docs/cli/index.md`
- `docs/cli/command-reference.md`

Vault and storage coverage:

- `docs/vaults/index.md`
- `docs/vaults/local-vaults.md`
- `docs/vaults/s3-r2-vaults.md`
- `docs/vaults/sync.md`
- `docs/vaults/encryption.md`
- `docs/vaults/secrets-and-key-export.md`
- `docs/vaults/backup-and-recovery.md`

Cost-control coverage:

- `docs/cost-control/index.md`
- `docs/cost-control/request-budgets.md`
- `docs/cost-control/price-budgets.md`
- `docs/admin/s3-cost-guardrails.md`

Admin and sharing coverage:

- `docs/admin/users-groups-roles.md`
- `docs/admin/operator-emails.md`
- `docs/sharing/index.md`

Reference and troubleshooting:

- `docs/reference/runtime-paths.md`
- `docs/reference/configuration.md`
- `docs/troubleshooting/install-troubleshooting.md`
- `docs/troubleshooting/general-troubleshooting.md`

## Product Facts Reflected In Docs

- Install path: signed APT repo via `https://apt.vaulthalla.sh/install.sh`, manual APT setup, or local `./bin/vh/install.sh`.
- Production source installs are discouraged; `sudo make install -- -d` is development-only and volatile.
- Main CLI names are `vh` and `vaulthalla`.
- CLI socket is `/run/vaulthalla/cli.sock`; non-root CLI operators need the `vaulthalla` Linux group and app user UID mapping.
- Initial CLI admin UID should be bound with `vh setup assign-admin`.
- Lifecycle commands such as `setup db`, `setup remote-db`, `setup nginx`, `teardown db`, and `teardown nginx` require `sudo`.
- Main runtime paths: `/etc/vaulthalla/config.yaml`, `/run/vaulthalla`, `/var/lib/vaulthalla`, `/var/log/vaulthalla`, `/mnt/vaulthalla`, `/usr/share/vaulthalla/psql`, `/usr/share/vaulthalla-web`.
- Runtime services: `vaulthalla.service`, `vaulthalla-cli.socket`, `vaulthalla-cli.service`, `vaulthalla-web.service`, and `vaulthalla-swtpm.service`.
- S3/R2 vaults use API keys, bucket settings, sync strategy, conflict policy, upstream encryption, request budgets, and optional price budgets.
- S3/R2 sync strategies are `cache`, `sync`, and `mirror`.
- Request budgets and price budgets are separate cost-control systems.
- Request budgets cap operation pressure on LIST, HEAD, GET, PUT, COPY, DELETE, and downloaded bytes.
- Price budgets can operate globally, per provider, or per vault with modes `off`, `report`, `warn`, and `enforce`.
- Vault keys and internal secrets have separate export commands.
- Backup docs must not imply a one-command full backup or restore exists.
- Dashboard backup/recovery indicators are status or policy signals, not proof that backup work has run.

## Validation Commands

When the docs package tooling is available:

```bash
pmdocs validate --source main-docs
```

Use the installed skill helper for Markdown directive/frontmatter checks:

```bash
python3 .agents/skills/payload-markdown/scripts/check_payload_markdown_doc.py docs/**/*.md
```

If the helper path changes, find it under the installed `payload-markdown` skill and keep validation notes in the final handoff.
