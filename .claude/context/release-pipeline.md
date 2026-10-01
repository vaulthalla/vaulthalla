# Release pipeline: versioning, CI, packaging, publication

## Versioning

`VERSION` is canonical (currently `1.6.6`). Release-managed files: `meson.build`, `web/package.json`,
`debian/changelog` (top entry version `X.Y.Z-1`). A version bump commit touches exactly those 4 files.
Tags are `vX.Y.Z` (annotated when cut by the tool), and pushing a tag triggers the release workflow.

## Cutting a release (recommended procedure)

```bash
python3 -m tools.release cut-release patch            # local only: checks, suites, bump, commit, tag
python3 -m tools.release cut-release 1.6.7 --push     # resume/push: atomic push of main + v1.6.7
python3 -m tools.release release-status 1.6.7 --watch # follow release.yml via gh
```

`cut-release {patch|minor|major|X.Y.Z} [--push] [--branch main] [--remote origin] [--skip-tests] [--no-fetch]`:
- Refuses unless on `main` (or `--branch`), tracked files clean (untracked files are fine), and HEAD equals
  `origin/main` after a fetch.
- Runs `check` + `run-tests` (all fast suites), then `set-version`, verifies exactly the 4 managed files changed,
  commits `chore(release): vX.Y.Z`, creates an annotated tag, and with `--push` runs
  `git push --atomic origin HEAD:refs/heads/main refs/tags/vX.Y.Z`, then prints the run URL (`gh run list`, or
  the Actions URL pattern).
- Resumable: an untagged release commit on top of origin gets tagged (`cut-release X.Y.Z`); a local-only tag
  gets pushed. Refuses if the tag already exists on the remote, or when `patch|minor|major` is given while
  HEAD is an unpushed release commit (use the explicit version to resume).
- Warns if `debian/changelog`'s top entry predates the previous tag (see the changelog section).
- `make release` runs a local-only `cut-release $(RELEASE_PART)` (default `patch`) and never pushes.

**GitHub UI alternative:** publishing a release in the UI ("Draft a new release" → new tag `vX.Y.Z` → Publish)
creates the tag, and the tag push triggers the same workflow. The version bump must already be on `main`
(the tag must match `VERSION`, or `validate-release-state` fails). A draft release creates no tag and triggers
nothing. `github-release` updates the UI-created release in place: assets are attached/overwritten, the
UI title is kept, and generated notes are merged into the body (`release_notes_merge.py`).
Prefer the tool: it runs the gates before the tag exists and guarantees the bump commit is what gets tagged.

## `python3 -m tools.release`

`check` · `sync [--dry-run]` · `set-version X.Y.Z` · `bump {major,minor,patch}` · `cut-release` ·
`release-status` · `run-tests [--suite S] [--count-only]` · `build-deb [--dry-run] [--output-dir]` ·
`validate-release-artifacts` · `publish-deb [--require-enabled] [--lab-evidence F] [--allow-older-version]` ·
`lab-smoke` · `resolve-release-notes-base` · `record-release-success` · `changelog {draft, payload, release,
ai-check, ai-draft, ai-release, ai-compare}`.
Python deps: repo-root `requirements.txt`. AI model profiles: repo-root `ai.yml`.

## Changelog selection

The pipeline is deterministic first. Evidence artifacts are always written **before** source selection.
Candidate order by `RELEASE_AI_MODE`:
- `auto`: openai → local → cached-draft → manual
- `openai-only`: openai → cached-draft → manual
- `local-only`: local → cached-draft → manual
- `disabled`: cached-draft → manual

Every AI stage has a per-request timeout (`RELEASE_AI_PROVIDER_TIMEOUT_SECONDS`, default 180; emergency
triage 45), so an outage falls back instead of hanging; the package action also bounds the whole stage
(`RELEASE_CHANGELOG_TIMEOUT_SECONDS`, default 2700).

The `manual` source is stale-checked against `VERSION` **and** against the last shipped tag: the top entry's
trailer date must not predate the release-notes base tag's commit (a header-only `set-version` bump of the
previous release's notes fails). When `manual` is selected, the Debian top-entry refresh is skipped.
**Known gap (2026-09-30):** `OPENAI_API_KEY` is a *Production-environment* secret, but `release-artifacts`
has no environment, so the key is invisible there; v1.6.6 shipped `selected_path: manual` with an April
changelog body. Until the key is a repository secret (or a build environment is added), releases need a
hand-written top entry, or the stale guard fails `release-artifacts` (before anything is published).
Schemas: `vaulthalla.release.ai_payload.v1`, `semantic_payload.v1`, `ai_triage.v2`, `changelog_selection.v2`.

Changelog relevance scoring (`tools/release/changelog/scoring.py` `is_semantic_noise_path`) treats all agent
tooling (`CLAUDE.md`, `.claude/`, and the legacy `.codex/` and `.agents/`) as derived noise. It's pinned by
`tests/changelog/test_scoring_noise_paths.py`. Keep both current if the agent layout moves again.

## GitHub workflows

- `build_and_test.yml`: push/PR to `main`. Job `build` (composite `runner`: core build → core tests → web) and
  job `tooling` (`check`, `run-tests`, shellcheck). CI builds with `meson setup build -Dbuild_unit_tests=true -Dinstall_data=false`.
- `release.yml`: tag `v*` or `workflow_dispatch` (inputs `ref`, `publish`, `release_notes_base`). DAG:
  `validate-release-state → core-verify, release-tooling-verify, web-verify, docs-validate → release-artifacts
  → [lab-smoke] → publish-debian → github-release → record-release-success`; `docs-publish` needs only
  `docs-validate` + `publish-debian`.
  - Permissions default to `contents: read` (+`actions: read`); `github-release`/`record-release-success` get
    `contents: write`, `docs-publish` `id-token: write`. Secrets are step-scoped: Nexus creds on the publish
    step, AI keys + `GITHUB_TOKEN` on the package step, `GITHUB_TOKEN` on base resolution.
  - Every job has `timeout-minutes`. `publish-debian` has job concurrency `vaulthalla-apt-publish` (one APT
    publication at a time across tags, queued, never cancelled); `lab-smoke` has `vaulthalla-lab-host`.
  - Every checkout uses `${{ github.event.inputs.ref || github.ref }}`; the `build` composite action no longer
    re-checks-out (it used to silently reset dispatch runs to `github.sha`).
  - `RELEASE_PUBLISH_REQUIRED=auto` resolves to true on tag refs or dispatch `publish=publish`.
  - Publication modes: `disabled` | `nexus`. `publish-debian` runs the idempotent, sha256-verified
    `publish-deb` (see `tools/release/packaging/PUBLICATION.md`).
  - `record-release-success` depends on `publish-debian` + `github-release` only: a docs failure no longer marks
    a live APT release as failed. `docs-publish` retries `pmdocs push` 3× and can be retried alone.
  - Downstream jobs use `!cancelled() && needs.X.result == 'success'` so the optional (skipped) `lab-smoke`
    never skips publication.
  - `Production` environment: **no protection rules** (checked 2026-09-30, `can_admins_bypass: true`). It holds
    secrets `NEXUS_USER`, `NEXUS_PASS`, `OPENAI_API_KEY` and variables `NEXUS_REPO_URL=https://apt.vaulthalla.sh`,
    `RELEASE_PUBLISH_MODE=nexus`, `VH_AI_RELEASE_PROFILE` (legacy `RELEASE_AI_PROFILE_OPENAI` still honored), `RELEASE_DEBIAN_DISTRIBUTION`, `RELEASE_DEBIAN_URGENCY`.
    Environment-scoped vars/secrets are only visible to jobs that declare `environment: Production`.
  - Docs: `pmdocs validate --source docs` then `pmdocs push` to `DOCS_SYNC_ENDPOINT` (vaulthalla.io).
- Composite actions (`.github/actions/`): `runner`, `build`, `test`, `setup_web`, `build_web`, `sync_web_icons`,
  `test_web`, `package`. `.github/scripts/shellcheck.sh` (maintainer scripts blocking, `bin/**/*.sh` advisory),
  `.github/actionlint.yaml` (self-hosted labels for local `actionlint`). Runners are self-hosted
  (`[self-hosted, Linux, X64, ubuntu-latest-lts]`) because they need the private icon dir.

### Re-run rules

- A failed `publish-debian`/`github-release`/`docs-publish`/`record-release-success`: **Re-run failed jobs**. The
  run's artifacts are reused, `publish-deb` skips what is live (identical sha256) and re-verifies.
- Never "Re-run all jobs" after `publish-debian` uploaded: the rebuild produces different bytes and `publish-deb`
  refuses to overwrite the published version (by design). Cut a new patch version instead.

## Optional pre-publish lab gate (`lab-smoke` job, default OFF)

Runs between `release-artifacts` and `publish-debian` when the **repository** variable
`VH_LAB_SMOKE_ENABLED=true` (a job-level `if` cannot see environment variables). It runs on
`[self-hosted, vh-lab]` in environment `Lab`, downloads the release artifact, and runs
`lab-smoke --host ${VH_LAB_HOST:-vh-storage} --deb <deb> --from-version <base>-1 [--pg-restart]`
(`VH_LAB_FROM_VERSION` overrides, `none` disables; `VH_LAB_SMOKE_PG_RESTART=true` adds the P0-1 check). Its
evidence is uploaded and `publish-debian` passes it as `--lab-evidence`, so only the exact sha256 that passed
the lab can publish. Enable only after a `vh-lab` runner with ssh access exists; otherwise the job queues.

`lab-smoke` (CLI): `--host H (--deb P | --apt-version V) [--from-version V] [--evidence F] [--pg-restart]
[--reboot] [--unit U]... [--settle-seconds 15] [--install-timeout 900]`. **Mutates the host.** Every remote
command runs as `ssh H timeout -k 5 N bash -c …` (plus a local timeout); nothing is killed. Aborts before
installing if dpkg is not clean. Asserts: `ii` at the expected version, empty `dpkg --audit`, units active with
stable `NRestarts` over the settle window, FUSE mounted (mountinfo), fusectl `waiting == 0`, `timeout 10 stat`
only after waiting is 0, `sudo timeout 15 vh status`, and, for the candidate phases (not `--from-version`),
legacy `vaulthalla-cli.{socket,service}` `not-found/inactive` plus `sudo test -S /run/vaulthalla/cli.sock` (#110).
Default units are `vaulthalla.service` and `vaulthalla-web.service`. It also checks sha256-identical `/etc/vaulthalla/config.yaml` and
`/etc/vaulthalla/testing/providers.env` (when present before). Evidence schema `vaulthalla.release.lab_smoke.v1`.

## Package action artifact contract

`resolve-release-notes-base` → `build-deb` → `validate-release-artifacts` produces:
`changelog.release.md`, `changelog.raw.md`, `changelog.payload.json`,
`changelog.semantic_payload.json`, `changelog.context.json` (required by validate), `release_notes.md`,
`changelog.selection.json`, `release_notes_base.resolution.json`, `SHA256SUMS`, plus the `.deb` and web tarball.
`build-deb` writes `SHA256SUMS` (coreutils format: debs, buildinfo/changes, web tarball); `validate` verifies it
(or emits it if absent) and checks the shipped `usr/share/vaulthalla/config/config.yaml` is byte-identical to
`deploy/config/config.yaml`. `SHA256SUMS` is attached to the GitHub release (`sha256sum -c SHA256SUMS --ignore-missing`).
`shipped_release.py` maintains the release success ledger used to pick the next release-notes base.

## Legacy/manual path

`make deb` → `bin/install_deb.sh` builds locally only; `--push` is retired and exits with a pointer to
`cut-release`. The only uploader is `publish-deb` (CI, or an emergency manual run with the same overwrite
protection). `make release` = local-only `cut-release`.

## Guardrail tests

`python3 -m tools.release run-tests` runs every fast suite with per-directory minimum counts
(`tools/release/suites.py`): `release` (`tools/release/tests`: changelog / packaging / root), `lifecycle`
(`deploy/lifecycle/tests`), `lab` (`tools/lab/tests`). A directory dropped from discovery (missing `__init__.py`,
missing dir) fails before tests run. Raise the floors when adding tests. CI uses only this runner (a contract test
forbids raw `unittest discover` in workflows). Keep green: `packaging/test_release_workflow_contract.py`
(workflow DAG, permissions, secrets scoping, timeouts, lab gate), `test_debian_publication.py` (idempotency,
integrity, curl credentials), `test_release_artifact_validation.py`, `test_cut_release.py`, `test_lab_smoke.py`,
`test_suites_guard.py`, `changelog/test_manual_changelog_stale_guard.py`, plus the changelog/AI suites.
Unit tests never call live OpenAI/local LLM providers, the network, ssh, or a real remote (cut-release tests use
a local bare repo).
