# Validation Guide

This is the "what should I run before opening a PR?" guide.

Run the narrowest checks that actually prove your change. Then be honest about what you did and did not validate.

## Quick Reality Check

Useful repo helpers:

```bash
bash .codex/scripts/doctor.sh
bash .codex/scripts/changed.sh all
bash .codex/scripts/verify.sh web
bash .codex/scripts/verify.sh release
bash .codex/scripts/verify.sh all
```

Those scripts are helpful shortcuts, not a substitute for subsystem-specific judgment.

## Docs-Only Changes

There is no dedicated markdown or docs validation pipeline documented in this repo today.

For docs-only PRs, do this at minimum:

- verify every command against the repo
- verify every referenced path exists
- avoid claims about infrastructure or workflows that are only planned
- read the rendered markdown in your editor and check relative links

If the change is truly docs-only, code tests are usually not required.

A lightweight docs validation check for relative links and referenced paths would be useful future work, but it is not required for docs-only PRs today.

## Web And Frontend Changes

Run:

```bash
cd web
pnpm test
```

That runs the repo's web check path:

- `pnpm run typecheck`
- `pnpm run lint`

Useful extra validation:

```bash
cd web
pnpm dev
```

Use that for manual checks when you changed UI behavior.

You can also run the repo helper from the root:

```bash
bash .codex/scripts/verify.sh web
```

## C++ Core Changes

Use the CI-style Meson build path:

```bash
meson setup build --buildtype=debug -Dbuild_unit_tests=true -Dinstall_data=false
meson compile -C build
meson test -C build
```

If your change is in core code and you did not build it on Linux, say so.

## FUSE And Filesystem Changes

FUSE work needs more than a compile.

Use the Linux integration path when possible:

```bash
./bin/tests/install.sh --run
./bin/tests/uninstall.sh
```

Also capture the manual behavior you checked, for example:

- mount comes up cleanly
- file operations behave as expected
- unmount and shutdown behavior are sane
- no obvious regressions in permissions or cache behavior

FUSE changes without Linux runtime validation are incomplete.

## CLI Changes

If you changed CLI parsing, usage text, shell protocol behavior, or lifecycle commands:

```bash
meson setup build --buildtype=debug -Dbuild_unit_tests=true -Dinstall_data=false
meson compile -C build
./bin/tests/install.sh --run
```

For help or manpage-related work, also inspect the affected usage definitions under `core/usage/*` and confirm the output is coherent.

## Packaging Changes

Run:

```bash
python3 -m tools.release check
python3 -m tools.release build-deb --dry-run
python3 -m unittest discover -s tools/release/tests -p 'test_*.py'
```

Then describe the lifecycle validation you performed:

- install
- upgrade if relevant
- remove
- purge
- service start and stop behavior
- config and local-state preservation

If you did not test package lifecycle on a Linux host or VM, say so.

## Database Or Schema Changes

Schema and DB-layer changes need more than "the SQL looked fine."

At minimum:

```bash
./bin/tests/install.sh --run
```

If packaging or release behavior is involved, also run:

```bash
python3 -m tools.release build-deb --dry-run
python3 -m unittest discover -s tools/release/tests -p 'test_*.py'
```

Use a disposable PostgreSQL-backed environment when you can. Document migration or upgrade assumptions clearly.

## Security-Sensitive Changes

Before you even get to validation, read [Security-Sensitive Work](./security-sensitive-work.md).

If the change was approved for public work, do not rely on partial validation. Sensitive changes should include:

- focused tests or reproducible manual checks
- clear impact description
- explicit note about anything you could not validate

## Release Tooling Changes

Run:

```bash
python3 -m tools.release check
python3 -m unittest discover -s tools/release/tests -p 'test_*.py'
```

Useful helper:

```bash
bash .codex/scripts/verify.sh release
```

If the change affects packaging outputs, add:

```bash
python3 -m tools.release build-deb --dry-run
```

## What To Put In The PR

A good validation note says:

- what you changed
- what commands you ran
- what manual checks you performed
- what you could not test

Examples:

- "Docs-only. Verified all commands and paths against the repo. No code paths changed."
- "Ran `cd web && pnpm test` and manually checked the updated admin page under `pnpm dev`."
- "Ran Meson unit-test build plus `./bin/tests/install.sh --run` on Linux. Did not validate upgrade behavior."

That level of honesty is enough. Hand-wavy claims are not.
