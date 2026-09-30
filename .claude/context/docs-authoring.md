# Consumer docs (`docs/`)

`docs/` is operator and end-user documentation: install, configure, operate, back up, recover,
troubleshoot, and use. Keep implementation internals out unless they change an operator decision.
`docs/contributors/` (8 pages) is contributor-facing, and `docs/dev/` holds implementation notes.

Authoring rules come from the `payload-markdown` and `payload-markdown-docs` skills in `.claude/skills/`.
Those are vendored by `pmdocs install skill --claude --docs-root ./docs --package-manager pnpm --force`, so
don't hand-edit them; reinstall to upgrade.

## Project conventions

- Every page has YAML frontmatter with supported fields only: `title navTitle description order status tags`.
  Use `status: published` for complete pages and `draft` only when a page is intentionally incomplete.
- Use root-relative route links (`/vaults/sync`), never `./sync.md`.
- Long pages get a compact TOC after the intro: `:::toc[On this page]{depth="3" theme="compact"}` / `:::`.
- Never commit generated AI exports (`llms.txt`, `llms-full.txt`, `index.ai.yml|yaml`).
- Validate with `python3 .claude/skills/payload-markdown/scripts/check_payload_markdown_doc.py <files>`,
  then `pmdocs validate --source docs`. CI's `docs-validate` job runs the latter, and `docs-publish` pushes it.

## Sections

`getting-started/` · `web-console/` · `cli/` (incl. `command-reference.md`) · `vaults/` (local, S3/R2,
sync, encryption, secrets/key export, backup & recovery, S3 gateway) · `cost-control/` (request vs
price budgets) · `admin/` (users/groups/roles, operator emails, S3 guardrails, S3 gateway) ·
`sharing/` · `s3-gateway/` (8 pages) · `reference/` (runtime paths, configuration) ·
`troubleshooting/`.

## Product facts docs must stay consistent with

- Install: `curl -fsSL https://apt.vaulthalla.sh/install.sh | bash`, manual APT, or `./bin/vh/install.sh`.
  Source installs (`make install` / `make dev`) are development-only.
- CLI names: `vh`, `vaulthalla`. Socket: `/run/vaulthalla/cli.sock`. Non-root operators need the `vaulthalla`
  group plus a UID mapping via `vh setup assign-admin`. `setup db|remote-db|nginx` and `teardown db|nginx` need `sudo`.
- Services: `vaulthalla` (owns `/run/vaulthalla/cli.sock`), `vaulthalla-web`, `vaulthalla-swtpm`. `vaulthalla-cli.socket`/`.service` were retired in #110; mention them only as legacy units that upgrades remove.
- S3/R2 sync strategies: `cache | sync | mirror`. Request budgets and price budgets are separate systems.
  Price budget scopes are global, provider, and vault; modes are `off | report | warn | enforce`.
- Vault keys and internal secrets have separate export commands.
- **Never imply a one-command full backup/restore exists.** Dashboard backup indicators are policy or status
  signals, not proof that a backup ran.
