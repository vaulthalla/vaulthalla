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

Remote-backed gateway buckets bind to Vaulthalla S3/R2 vaults. Writes and deletes flow through Vaulthalla local file state first. Existing sync behavior owns upstream upload, purge, remote index, manifest, request budget, storage class, and encryption metadata side effects.

For smart-cache buckets, remote-only indexed objects can be listed and headed without downloading bodies or touching upstream. A GET can download and materialize the object according to the vault policy.

Accounting follows the same rule. LIST and metadata-backed HEAD are metadata-only and do not consume upstream request budgets. GET consumes upstream request and price budgets only when the gateway actually downloads from the provider. If the object is already present as a local encrypted file or a materialized remote-cache file, the gateway serves it locally and records no upstream provider usage by default.

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

Remote-backed gateway requests can consume provider cost one operation at a time. Vaulthalla estimates actual upstream work before that work starts and evaluates:

- Global price policies.
- Provider price policies.
- Vault price policies.
- Per-gateway-key monthly policies.
- Per-gateway-key/per-vault monthly policies.

Local gateway buckets, cache hits, metadata hits, and local-first PUT/DELETE/COPY/multipart completion are not charged against remote S3 provider budgets at gateway request time. For remote-backed writes and deletes, sync later owns the upstream provider/vault/global ledger entry when it performs the actual provider upload or purge.

```bash
# Admin-managed key-wide cap.
vh s3-gateway budget set-key backup --monthly 5 --mode enforce

# Vault owners/admins can manage caps for key/vault pairs they control.
vh s3-gateway budget set-key-vault backup --vault archive --monthly 2 --mode warn
vh s3-gateway budget status --key backup --vault archive
```

Budget ledger rows include the gateway credential id, vault id, operation, request UUID, object key when available, and estimated cost. This makes it possible to audit which inbound key consumed gateway budget.

The credential option `enforce_budget_for_local_requests` changes only per-key gateway accounting. When disabled, local/cache hits are free from gateway key and key/vault budgets. When enabled, local/cache/sync-deferred requests are charged as synthetic gateway usage and ledger/status output marks them with `synthetic=true` and a source such as `local_cache`, `local_file`, `metadata`, or `sync_deferred`. Synthetic local usage never pollutes global/provider/vault upstream accounting.

Budget denial XML includes the exact blocking policy id, scope, window, limit, used-before amount, remaining-before amount, requested cost, currency, provider key, vault id, credential id, operation, and request UUID. If several enforce policies are exceeded, Vaulthalla keeps the whole blocker chain and chooses the top-level blocker deterministically.

Remote-backed gateway operations also use the vault's existing S3/R2 request budgets. These failures return S3 XML with `SlowDown` and identify the request kind, such as `GET`, `PUT`, `DELETE`, `COPY`, or downloaded bytes, separately from price-budget denials.

## Metadata And ETags

S3 ETags are client-visible gateway metadata. They are not the same thing as Vaulthalla local `content_hash`.

- Single PUT ETag: MD5 of the S3 request body, quoted.
- Multipart ETag: S3-style MD5 of part MD5 digests plus `-part_count`, quoted.
- `x-amz-meta-*` headers are preserved as gateway object metadata.
- `Content-Type` is preserved when provided and falls back to `application/octet-stream`.
- Existing Vaulthalla files without gateway ETags use metadata-derived fallback ETags during LIST/HEAD. LIST never decrypts or reads plaintext just to calculate an ETag.

Use explicit backfill when importing existing Vaulthalla metadata into gateway object state:

```bash
vh s3-gateway bucket backfill archive
vh s3-gateway bucket backfill archive --calculate-etags
```

The default backfill is metadata-only. `--calculate-etags` intentionally reads/decrypts local file bodies and prints a warning.

## Delete Behavior

Gateway delete is currently unversioned from the S3 client perspective.

Local buckets remove local Vaulthalla object state, object metadata, backing/cache files, and fs cache mappings. Remote-backed buckets mark or remove Vaulthalla local object state first, clear gateway metadata, and hide the object from future S3 LIST/HEAD/GET immediately.

The gateway does not directly delete upstream S3/R2 objects as the primary DELETE behavior. Existing sync planning and tasks observe the local delete/tombstone and own upstream purge. After a successful gateway delete, future FUSE, web, sync, and S3 calls should not observe the deleted object's old inode, old shared object pointer, or gateway metadata row. The remote index may still contain the upstream object until sync corrects or purges it, but gateway visibility follows Vaulthalla local state.

## When To Use API-Exclusive Buckets

API-exclusive buckets are intended for buckets created and owned primarily through the S3 gateway. They keep S3 client expectations clearer by avoiding unrelated Vaulthalla workflows creating conflicting object state in the same vault.

Use API-exclusive remote-cache buckets for proxy/cache workloads where Vaulthalla should expose an S3-compatible edge while still relying on remote object index and manifest sync instead of broad upstream scans.
