# S3 Gateway Vaults

The S3 gateway maps S3 concepts onto Vaulthalla vaults:

- S3 bucket -> Vaulthalla vault
- S3 object key -> vault-relative path
- S3 object metadata -> gateway object metadata rows
- Local bucket content -> encrypted Vaulthalla local backing files
- Remote-backed bucket content -> Vaulthalla S3/R2 vaults through `CloudEngine`

## Local Buckets

Local gateway buckets use Vaulthalla local vault encryption. Clients can use standard S3 tools to PUT, GET, HEAD, LIST, COPY, multipart upload, and DELETE objects while Vaulthalla stores encrypted local content.

```bash
vh s3-gateway bucket create-local archive
aws --endpoint-url http://127.0.0.1:39000 s3 cp ./report.pdf s3://archive/reports/report.pdf
```

Nested object keys create Vaulthalla parent directories as needed. Directory marker objects are accepted as zero-byte keys ending in `/`, but they are kept distinct from normal Vaulthalla directories.

## Remote-Backed Buckets

Remote-backed gateway buckets bind to Vaulthalla S3/R2 vaults. Writes flow through local Vaulthalla file state and then upload with `CloudEngine`, preserving remote index, manifest, request budget, storage class, and encryption metadata behavior.

For smart-cache buckets, remote-only indexed objects can be listed and headed without downloading bodies. A GET can download and materialize the object according to the vault policy.

## Scoped Gateway Access

Each inbound S3 gateway key authenticates as a principal Vaulthalla user. The principal's normal RBAC must allow the action, and the key's gateway scope must also allow the vault and action.

`vault_allowlist` credentials can expose one or more vaults with independent action flags:

| Flag | S3 gateway operations |
| --- | --- |
| `list` | List buckets and objects. |
| `read` | HEAD/GET object and metadata reads. |
| `write` | PUT, COPY destination writes, and multipart upload writes. |
| `delete` | Object delete and multi-delete. |
| `admin` | Bucket bind/unbind/create/delete style administration. |

Examples:

```bash
# Single-bucket backup key.
vh s3-gateway creds create backup \
  --scope vault-allowlist \
  --vault archive \
  --list --read --write \
  --json

# Multi-vault read-only analytics key.
vh s3-gateway creds create analytics \
  --scope vault-allowlist \
  --vault archive \
  --vault reports \
  --list --read \
  --json
```

Use `user_access` for a personal key that follows the user's normal vault permissions. Use `global` only for admin-created service keys that need all gateway bucket bindings and are audited through `created_by` and `principal_user_id`.

## Gateway Price Budgets

Remote-backed gateway requests can consume provider cost one operation at a time. Vaulthalla estimates that operation before upstream work and evaluates:

- Global price policies.
- Provider price policies.
- Vault price policies.
- Per-gateway-key monthly policies.
- Per-gateway-key/per-vault monthly policies.

Local gateway buckets are not charged against remote S3 provider budgets.

```bash
# Admin-managed key-wide cap.
vh s3-gateway budget set-key backup --monthly 5 --mode enforce

# Vault owners/admins can manage caps for key/vault pairs they control.
vh s3-gateway budget set-key-vault backup --vault archive --monthly 2 --mode warn
vh s3-gateway budget status --key backup --vault archive
```

Budget ledger rows include the gateway credential id, vault id, operation, request UUID, object key when available, and estimated cost. This makes it possible to audit which inbound key consumed gateway budget.

## Metadata And ETags

S3 ETags are client-visible gateway metadata. They are not the same thing as Vaulthalla local `content_hash`.

- Single PUT ETag: MD5 of plaintext body, quoted.
- Multipart ETag: S3-style MD5 of part MD5 digests plus `-part_count`, quoted.
- `x-amz-meta-*` headers are preserved as gateway object metadata.
- `Content-Type` is preserved when provided and falls back to `application/octet-stream`.

## Delete Behavior

Gateway delete is currently unversioned and destructive.

Local buckets remove local Vaulthalla object state, object metadata, backing/cache files, and fs cache mappings. Remote-backed buckets delete upstream S3/R2 first, then remove Vaulthalla object state, remote index rows, manifests, local cache/backing bytes, and stale inode/path/id mappings.

After a successful gateway delete, future FUSE, web, sync, and S3 calls should not observe the deleted object's old inode, old shared object pointer, remote index entry, or gateway metadata row.

## When To Use API-Exclusive Buckets

API-exclusive buckets are intended for buckets created and owned primarily through the S3 gateway. They keep S3 client expectations clearer by avoiding unrelated Vaulthalla workflows creating conflicting object state in the same vault.

Use API-exclusive remote-cache buckets for proxy/cache workloads where Vaulthalla should expose an S3-compatible edge while still relying on remote object index and manifest sync instead of broad upstream scans.
