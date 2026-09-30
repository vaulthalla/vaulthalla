---
name: release-prep
description: Prepare a vaulthalla version bump and release (VERSION, meson.build, web/package.json, debian/changelog), run the pre-tag checks, and explain what the tag-triggered release workflow will do. Use when asked to bump the version, cut or tag a release, or check release readiness.
---

# Release prep

Background reading: `.claude/context/release-pipeline.md`.

## Steps

1. **Preconditions.** Be on a clean branch off `main`, and check that `python3 -m tools.release check` passes on the current state.
2. **Bump.** Choose the semver level from the change set: patch for fixes, minor for features or new migrations/commands,
   major for breaking config, CLI, or package contracts.
   ```bash
   python3 -m tools.release bump patch        # or: set-version X.Y.Z
   python3 -m tools.release sync --dry-run    # should report nothing left to change
   git diff --stat                            # expect exactly: VERSION meson.build web/package.json debian/changelog
   ```
   Repo convention for the commit message: `bump version to X.Y.Z and update changelog, meson.build, package.json, and VERSION file`.
3. **Verify.** Run `bash tools/dev/verify.sh release packaging`, plus the profiles for whatever
   shipped since the last tag (`git log vPREV..HEAD --stat`). Report the known packaging failure honestly if it's still present.
4. **Changelog preview (optional, offline).** `python3 -m tools.release changelog draft --format raw`. AI stages need
   `OPENAI_API_KEY` and are run by CI. Don't call live providers locally unless the user asks.
5. **Lab gate (recommended for anything touching packaging or runtime).** Build the `.deb` and upgrade-test it on
   vh-storage with the `/lab` skill **before** tagging. Publishing is irreversible: the tag triggers
   `publish-debian` to `apt.vaulthalla.sh stable`, and real hosts pull it.
6. **Tag and push only on explicit user instruction.** `git tag vX.Y.Z && git push origin vX.Y.Z` triggers
   `release.yml`: 10 jobs, with the `Production` environment gating publish/docs/ledger.

## Don'ts

- Don't hand-edit versions in the managed files. Use `set-version`/`bump` so `check` stays green.
- Don't use `make release` / `bin/install_deb.sh --push` (the legacy Nexus push path) unless the user asks for it.
- Don't create tags, push, or trigger workflow_dispatch without being told to in this conversation.
