# AI Contributions

This document records, at a high level, where AI tools have done substantial implementation work in
Vaulthalla. It is development provenance, not an inventory of every AI-assisted line, commit, or file.
Git history remains the detailed record.

## Pre-agent development: ChatGPT as an advisory tool

Vaulthalla was conceived, architected, and built by its maintainer, starting in June 2025. For roughly the
first eleven months the code was written by hand. ChatGPT was used throughout that period as an advisory
tool: architecture discussion, debugging, implementation advice, design review, research, and second
opinions. It did not write the implementation.

The fundamental architecture and the original implementation of the core system are human-authored. That
includes the daemon's shape and runtime model, the system boundaries and data model, and the major original
subsystems: the WebSocket and HTTP layers, the Linux CLI and the web console's admin/command surfaces, the
FUSE daemon and its integration, the RBAC and authentication model, storage abstractions, and the way these
pieces connect. Those areas have since received, and will keep receiving, incremental fixes and refactors,
some of them AI-assisted; their design and original implementation came from the maintainer.

## OpenAI Codex (from April 2026)

Codex was the first agent used for direct implementation inside the existing architecture. It implemented
substantial parts of:

- Debian packaging fixes and package-lifecycle hardening.
- The link-sharing feature (public and email-validated shares with RBAC integration).
- S3 cost control: sync safety, request and price budgets, and storage-tier pricing.
- The stats and admin health dashboard (`stats.*` commands and the admin health center).

## Anthropic Claude (from September 2026)

Claude Code was used for the production-hardening work released in 1.7.0. It performed hardening work on:

- Debian install, upgrade, remove, purge, and reinstall behavior: maintainer scripts that cannot hang on
  the FUSE mount, purge on a mounted data disk, handling of a database left behind by a purge, default
  config handling, and nginx setup.
- systemd and service lifecycle: bounded stops and restarts, start limits, restarting failed units on
  upgrade, and retiring the separate CLI socket units.
- Database robustness: connection-pool recovery after PostgreSQL restarts, and accepting migrations that
  had been edited after release so older installs can upgrade.
- Authentication and session security: server-side enforcement of the default admin password change,
  login rate limiting, RBAC fixes for self-promotion and role overwrites, user-update targeting, and
  session cookie policy.
- WebSocket session stability: handler exceptions, session close, and handshake races that could abort
  the daemon.
- CLI robustness and CLI/web console parity fixes.
- Release and install validation: CI test discovery, idempotent checksum-verified APT publishing, release
  cutting, real-host smoke and parity tooling (`tools/lab/`), and DeepSeek support for release-note
  generation.

Expected direction, not yet implemented: incremental architecture improvements such as a shared CLI command
and dispatch layer to reduce duplicated request-handling logic between the Linux CLI and the web console.

## Policy

- AI assistance is permitted in this project. See
  [AI-Assisted Contributions](docs/contributors/ai-assisted-contributions.md) for the standards it is
  held to.
- The person who submits or accepts a change is responsible for understanding, reviewing, testing, and
  maintaining it. Generated code is not merged because a model produced it; it is reviewed against the
  project's architecture and quality bar.
- AI tools are implementation aids, not maintainers of Vaulthalla.
- Provenance is recorded here per feature or workstream. Source files do not carry generated-by notices.
- This document does not replace Git history, [CONTRIBUTING.md](CONTRIBUTING.md), the copyright notices,
  or the AGPL-3.0 [license](LICENSE).
- Some areas may stay almost entirely human-written while others receive more agent implementation over
  time. This document should be updated as that changes.
