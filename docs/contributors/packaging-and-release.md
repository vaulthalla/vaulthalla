# Packaging And Release

This guide is for contributors touching Debian packaging, lifecycle scripts, systemd wiring, install helpers, release tooling, or publication-related code.

Packaging work is welcome.

Packaging work is also sharp.

The reason is simple: Vaulthalla packaging does not just copy files into place. It touches service wiring, runtime directories, FUSE expectations, PostgreSQL bootstrap, nginx integration, and TPM or `swtpm` setup.

## The Boundary

Contributors can help with packaging and release work, but package lifecycle changes should start with maintainer alignment.

Read this together with:

- [Contribution Boundaries](./contribution-boundaries.md)
- [Validation Guide](./validation-guide.md)
- [Security-Sensitive Work](./security-sensitive-work.md)

## Where Packaging Lives

| Surface | Paths | Why it matters |
| --- | --- | --- |
| Debian package metadata | `debian/control`, `debian/install`, `debian/changelog`, `debian/rules` | Defines build inputs, staged payload, and package metadata |
| Debian maintainer scripts | `debian/postinst`, `debian/prerm`, `debian/postrm` | Source of truth for install, upgrade, remove, and purge behavior |
| Systemd units | `deploy/systemd/*` | Defines daemon, CLI socket/service, web service, and `swtpm` runtime behavior |
| Lifecycle utility | `deploy/lifecycle/main.py` | Powers privileged `vh setup` and `vh teardown` flows |
| Dev/operator helpers | `bin/install.sh`, `bin/uninstall.sh`, `bin/setup/*`, `bin/teardown/*` | Useful local wrappers, but not the Debian lifecycle contract |
| Release automation | `tools/release/*`, `.github/actions/package/action.yml`, `.github/workflows/release.yml` | Owns dry-run packaging, artifact validation, and publication policy |

## Important Repo Reality

For Debian lifecycle behavior, the maintainer scripts are the source of truth:

- `debian/postinst`
- `debian/prerm`
- `debian/postrm`

Do not assume `bin/install.sh` or `bin/uninstall.sh` define the Debian contract. They are useful local helpers, but `apt install`, `apt remove`, `apt upgrade`, and `apt purge` are governed by the maintainer scripts.

## What Packaging Contributors Can Help With

Good packaging contributions include:

- lifecycle documentation
- contract tests and validation improvements
- focused, conservative bug fixes
- systemd or packaging clarity improvements that do not change behavior unexpectedly
- dry-run packaging or artifact validation fixes in `tools/release/`

What does not count as a good first packaging PR:

- changing install, upgrade, remove, and purge behavior without prior discussion
- rewriting maintainer scripts for style
- bundling package behavior changes together with unrelated runtime refactors
- changing service lifecycle semantics without explicit validation notes

## Why Packaging Changes Require Coordination

Packaging changes can affect:

- `/etc/vaulthalla/config.yaml`
- `/var/lib/vaulthalla`, `/var/log/vaulthalla`, `/run/vaulthalla`
- `/mnt/vaulthalla`
- local PostgreSQL bootstrap and teardown behavior
- nginx site installation and reload behavior
- `vaulthalla.service`, `vaulthalla-cli.service`, `vaulthalla-cli.socket`, `vaulthalla-web.service`, and `vaulthalla-swtpm.service`
- hardware TPM versus `swtpm` fallback selection

That is why package lifecycle work is "coordinate first" even when the code change looks small.

## Validation Expectations

If your PR changes packaging or lifecycle behavior, aim to validate these layers:

### Release-tooling and package dry run

```bash
python3 -m tools.release check
python3 -m tools.release build-deb --dry-run
python3 -m unittest discover -s tools/release/tests -p 'test_*.py'
```

### Lifecycle behavior

At minimum, document how you validated:

- fresh install
- upgrade behavior if applicable
- `apt remove`
- `apt purge`
- service start and stop behavior
- whether config and existing local state were preserved as expected

For packaging changes that touch optional integrations, note what happened with:

- PostgreSQL absent versus present
- nginx absent versus present
- hardware TPM absent versus present
- `swtpm` fallback path

If you did not run some of those checks, say so plainly.

## Service Lifecycle Expectations

Contributors should preserve these high-level expectations unless the maintainer explicitly agrees to change them:

- package scripts stay idempotent
- upgrades do not casually overwrite operator config
- upgrades do not destructively reset existing DB, nginx, TPM, or web cache state
- optional integrations do not make upgrades fail just because they are absent
- service stop/start behavior stays predictable and bounded

If your change intentionally alters one of those expectations, the PR needs a strong reason and explicit review.

## Release Work Versus Publication Work

Contributors can help with:

- release-tooling fixes
- changelog pipeline fixes
- dry-run packaging
- artifact validation
- fixture-backed test updates

Release publication is maintainer-owned unless explicitly delegated.

In practice that means contributors should stop at validated artifacts and reproducible dry runs. Do not expect to publish packages, use release credentials, or change publication settings unless the maintainer has asked you to do that specific work.

## Small PR Guidance

Good packaging PR:

- one focused lifecycle or release-tooling fix
- explicit note about operator impact
- validation notes for install/remove/purge or dry-run packaging

Bad packaging PR:

- maintainer-script rewrite plus unrelated web or core changes
- "cleanup" that silently changes lifecycle behavior
- publication-flow changes with no contract-test coverage
