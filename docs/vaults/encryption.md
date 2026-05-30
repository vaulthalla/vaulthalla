---
title: Encryption
description: Understand Vaulthalla TPM-backed keys, per-vault encryption, S3/R2 upstream encryption, key rotation, and export risk.
order: 240
status: published
tags:
  - vaults
  - encryption
  - security
---

# Encryption

Vaulthalla combines TPM-protected master keys, per-vault AES-256-GCM keys, per-file encryption metadata, and optional upstream S3/R2 encryption. Operators should understand these layers before changing vault encryption policy or planning recovery.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Key Layers

| Layer | Purpose |
| --- | --- |
| TPM or `swtpm` master material | Protects Vaulthalla master keys on the host. |
| Vault master key provider | Encrypts stored per-vault keys. |
| Per-vault AES-256-GCM key | Encrypts file content for a vault. |
| Per-file IV and key version | Lets Vaulthalla decrypt the correct ciphertext with the correct vault key version. |
| S3/R2 upstream encryption metadata | Records whether remote object bodies are Vaulthalla-encrypted and which IV/key version applies. |

## TPM Selection

At startup, Vaulthalla uses TPM configuration in this order:

1. Explicit TCTI overrides such as `VH_TPM_TCTI`, `TSS2_TCTI`, or `TPM2TOOLS_TCTI`.
2. Hardware TPM devices such as `/dev/tpmrm0` or `/dev/tpm0`.
3. The local `swtpm` service on the managed localhost ports.

If no usable TPM path exists, encryption initialization fails and the service cannot safely run.

## Software TPM State

When `swtpm` is used, its state is stored under:

```text
/var/lib/swtpm/vaulthalla
```

That state is sensitive. Back it up only into protected operator backup storage. Restoring it to a different host changes your trust and recovery model.

## Per-Vault Keys

Each vault has a generated 32-byte AES key. Vaulthalla encrypts that key with TPM-backed master material before storing it in PostgreSQL. File encryption uses AES-256-GCM and records IV and key version metadata so later reads can find the right key version.

Key rotation creates a new vault key version, decrypts with the old key version, re-encrypts with the current key version, and records rotation state until complete.

```bash
vh vault keys rotate <vault> --sync-now
vh vault keys rotate all
```

Use `--sync-now` when remote encrypted objects should be brought forward immediately where supported by the sync plan.

## S3/R2 Upstream Encryption

For S3/R2 vaults, upstream encryption controls whether object bodies uploaded to the bucket are encrypted by Vaulthalla before upload.

With upstream encryption enabled:

- Object payloads are encrypted before upload.
- Remote object metadata records Vaulthalla encryption status, IV, key version, and content hash.
- The bucket operator still sees object names and provider metadata.
- Recovery requires the database metadata and/or remote encryption metadata plus the correct vault keys.

With upstream encryption disabled:

- Object payloads written by Vaulthalla are plaintext from Vaulthalla's perspective.
- Provider-side encryption may still exist, but it is outside Vaulthalla's key model.
- Anyone with bucket read access and provider decryption rights may read uploaded object bodies.

## Changing Upstream Encryption

Changing upstream encryption on an existing S3/R2 vault is sensitive because objects may already exist in the bucket.

Examples:

```bash
vh vault update archive --no-encrypt --accept-decryption-waiver
vh vault update archive --encrypt --accept-overwrite-waiver
```

Use these waivers only after deciding how existing objects should be handled. For important buckets, take a backup, run a dry-run, and test on a copy of the bucket first.

## Export Vault Keys

Export keys for disaster recovery:

```bash
vh vault keys export all --recipient <gpg-fingerprint> --output vaulthalla-vault-keys.json.gpg
```

Exporting without a recipient writes unencrypted key material:

```bash
vh vault keys export all --output vaulthalla-vault-keys.json
```

:::callout{theme="danger" title="Unencrypted key exports are live recovery material"}
Anyone who obtains unencrypted vault keys plus enough vault metadata and file bodies may be able to decrypt vault content. Use GPG-encrypted exports and store them separately from routine host backups.
:::

## Export Internal Secrets

Internal secrets such as database and JWT secrets are managed separately from vault keys:

```bash
vh secret export all --recipient <gpg-fingerprint> --output vaulthalla-secrets.json.gpg
```

See [Secrets And Key Export](/vaults/secrets-and-key-export).

## CPU Requirements

Vaulthalla's AES-256-GCM path depends on supported CPU and crypto-library features. Production hosts should have AES/PCLMUL-capable hardware support. Development-only overrides may exist, but they are not a production escape hatch.

If the service fails during crypto initialization, check [Install Troubleshooting](/troubleshooting/install-troubleshooting).
