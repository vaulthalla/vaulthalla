# Security-Sensitive Work

If your change touches auth, permissions, crypto, secrets, local trust boundaries, or privileged host behavior, slow down and read this first.

Vaulthalla is infrastructure software. Security-sensitive work is not restricted because contributions are unwelcome. It is restricted because a sloppy patch here can create real data loss, privilege escalation, or secret-handling failures.

## What Counts As Security-Sensitive

This includes work in or around:

- authentication and session handling
- access tokens and refresh tokens
- RBAC and permission resolution
- encryption and decryption behavior
- key management and secret storage
- local socket trust boundaries
- privileged lifecycle commands
- install scripts that modify system state
- TPM and `swtpm` behavior
- preview or proxy routes that can expose data unexpectedly

Common repo surfaces:

- `core/src/auth/*`
- `core/src/rbac/*`
- `core/src/crypto/*`
- `core/src/protocols/shell/*`
- `web/src/app/api/auth/session/route.ts`
- `web/src/stores/useWebSocket.ts`
- `debian/postinst`, `debian/prerm`, `debian/postrm`
- `deploy/lifecycle/main.py`
- `deploy/systemd/vaulthalla-swtpm.service.in`

## Decide Which Path To Use

| Situation | What to do |
| --- | --- |
| You found a likely auth bypass, privilege escalation, secret disclosure path, or exploitable trust-boundary failure | Do not open a public issue with exploit details. Report it privately. |
| You want to harden a sharp area but are not reporting an active vulnerability | Open an issue first and ask for maintainer guidance before implementing. |
| You are improving docs, tests, comments, or validation around a sensitive area without exposing exploit details | A normal public PR is usually fine, but keep scope narrow. |
| You are changing auth, RBAC, crypto, session, TPM, or privileged lifecycle behavior | Get maintainer approval before writing the patch. |

## Private Reporting

If you believe you found a real vulnerability:

1. do not post exploit details in a public issue or PR
2. ask for a private reporting route if one is not already published
3. share reproduction, impact, and affected paths privately

This repository does not currently document a dedicated security mailbox in-tree. If no private contact route is published at the time you are reporting, open a minimal issue asking for a private channel without posting the vulnerability details themselves.

Before broader external contribution, Vaulthalla should publish a dedicated `SECURITY.md` or enable a private vulnerability reporting route. Until then, do not post exploit details publicly; ask for a private channel first.

## Public Hardening Work

The following can still be reasonable public contributions when handled carefully:

- tighter validation around sensitive paths
- documentation that clarifies trust boundaries
- better error handling that does not change security behavior
- additional tests for already-approved fixes
- small defense-in-depth changes that have already been discussed

Public hardening work still needs to avoid dumping exploit writeups into the PR thread.

## What Requires Approval Before Implementation

Do not start coding first if the work changes:

- token issuance or refresh behavior
- session validation
- permission resolution
- vault or admin role semantics
- encryption, decryption, or key-rotation logic
- local socket authentication or UID trust behavior
- TPM backend selection or fallback logic
- package-time privileged setup with security impact

For those changes, the design discussion comes first.

## PR Expectations For Approved Sensitive Work

If the maintainer has approved a security-sensitive patch, keep the PR:

- narrow
- explicit about risk and impact
- honest about validation
- free of secrets, credentials, or exploit dumps

Useful things to include:

- affected paths
- threat or failure mode being addressed
- before and after behavior
- tests or manual validation run
- any remaining risk or follow-up work

## The Short Version

- Security work is welcome.
- Security theater is not.
- Public exploit PRs are not.
- When in doubt, ask first.
