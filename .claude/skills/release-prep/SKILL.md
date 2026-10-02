---
name: release-prep
description: Prepare a vaulthalla version bump and release (VERSION, meson.build, web/package.json, debian/changelog), run the pre-tag checks, and explain what the tag-triggered release workflow will do. Use when asked to bump the version, cut or tag a release, or check release readiness.
---

# Release prep

Background reading: `.claude/context/release-pipeline.md`.

## Steps

1. **Preconditions.** On `main`, tracked files clean, in sync with `origin/main`. `python3 -m tools.release check`
   passes and `python3 -m tools.release run-tests` is green (it enforces minimum test counts per suite).
2. **Choose the level** from the change set (`git log vPREV..HEAD --stat`): patch for fixes, minor for features or
   new migrations/commands, major for breaking config, CLI, or package contracts.
3. **Changelog body.** CI's AI path only runs if `OPENAI_API_KEY` is visible to `release-artifacts` (as of
   2026-09-30 it is a Production-environment secret, so it is not). Otherwise the manual fallback is used and it
   **fails** if the `debian/changelog` top entry predates the previous tag. Write a real top entry when needed.
   Offline preview: `python3 -m tools.release changelog draft --format raw`. Don't call live AI providers locally
   unless the user asks.
4. **Pre-release proof stays off the lab.** Prove packaging/runtime changes on the dev VM (`/verify`,
   `make run_test`, `python3 -m tools.release build-deb --output-dir /tmp/claude-release` + a local install) and with a
   CI dry run. **Never install the candidate on vh-storage**: it tests the real apt install/upgrade path and must sit
   on the published version (see `/lab`). Publishing is irreversible: a published version can never be replaced
   (publish-deb refuses different bytes under the same version).
5. **Cut locally** (no push). This runs check plus the suites, bumps exactly the 4 managed files, commits
   `chore(release): vX.Y.Z`, and creates an annotated tag:
   ```bash
   python3 -m tools.release cut-release patch      # or minor | major | X.Y.Z
   git show --stat HEAD                            # expect: VERSION meson.build web/package.json debian/changelog
   ```
6. **Push only on explicit user instruction.** `python3 -m tools.release cut-release X.Y.Z --push` resumes from the
   local tag and runs `git push --atomic` of `main` plus the tag. That triggers `release.yml`. Then:
   `python3 -m tools.release release-status X.Y.Z --watch`.
   The GitHub UI "publish release" with tag `vX.Y.Z` triggers the same workflow, but only after the bump commit is on `main`.

## After a failed run

- Use "Re-run failed jobs", never "Re-run all jobs" after `publish-debian` uploaded (a rebuild would be refused as an
  overwrite). A failed `docs-publish` doesn't affect the APT release or the success ledger.
- `publish-deb` is idempotent: identical bytes already live are skipped, and it re-verifies sha256 in the APT index.

## Don'ts

- Don't hand-edit versions in the managed files. Use `cut-release`, `set-version`, or `bump` so `check` stays green.
- `bin/install_deb.sh --push` is retired, and `make release` never pushes. The only uploader is `publish-deb`.
- Don't create tags, push, or trigger workflow_dispatch without being told to in this conversation.
