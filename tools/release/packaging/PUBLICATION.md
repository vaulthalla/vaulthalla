# Debian Publication and Live APT Validation

## CI publication contract

Publication is configured by:

- `RELEASE_PUBLISH_MODE` (`disabled` or `nexus`)
- `NEXUS_REPO_URL` (upload target; also the APT base used for index reads unless
  `RELEASE_APT_REPOSITORY_URL` is set)
- `NEXUS_USER`, `NEXUS_PASS` (step-scoped secrets; passed to curl on stdin via `--config -`, never argv)
- `RELEASE_APT_SUITE` / `RELEASE_APT_COMPONENTS` / `RELEASE_APT_ARCHITECTURES` (index location)

The `publish-debian` job runs:

```bash
python3 -m tools.release publish-deb --output-dir release --require-enabled [--lab-evidence lab/lab-smoke.json]
```

## Idempotency and integrity (a published version is immutable)

Nexus accepts re-uploads of an existing version (v1.6.4/1.6.5 re-runs replaced live bytes), so
`publish-deb` enforces immutability itself. For each staged `.deb`
(`<package>_<version>_<arch>.deb`):

| APT `Packages` index says | Action |
|---|---|
| version absent | upload |
| version present, same SHA256 | skip (success) |
| version present, different SHA256 | **fail** (`PublicationIntegrityError`) |
| version present, no SHA256 field | fail (identity cannot be proven) |
| absent, but a newer version is live | fail unless `--allow-older-version` |

Before planning, the `.deb` must match `SHA256SUMS` (written by `build-deb`, verified by
`validate-release-artifacts`); with `--lab-evidence`, it must also be the exact package lab-smoke
passed. The index is read fail-closed: if any component/architecture index is unreadable, nothing
is uploaded. After uploading, the index is polled (`--verify-attempts`, `--verify-delay`) until each
package is listed **with the expected SHA256**.

Re-running a failed `publish-debian` job is safe: it skips what is already live and re-verifies.
Re-running *all jobs* rebuilds the package (new bytes) and will be refused; use "Re-run failed jobs".

## Live validation

Real-host validation is `python3 -m tools.release lab-smoke` (see `.claude/context/release-pipeline.md`):
N-1 → N upgrade, units, FUSE, `vh status`, config integrity, optional PostgreSQL restart and reboot.
