# S3 Gateway

Vaulthalla can expose vaults through an S3-compatible gateway. The gateway is a separate runtime service named `S3GatewayService`; it is registered with the runtime manager, supervised by the watchdog, and reported separately from the HTTP preview server.

The gateway has two primary modes:

- `local`: a MinIO-like encrypted local object store backed by Vaulthalla local vaults.
- `remote_cache` / `remote_proxy`: S3/R2-backed buckets that use Vaulthalla S3 vault engines, remote object indexes, request budgets, remote manifests, and upstream encryption metadata.

## Configuration

The gateway is disabled by default:

```yaml
s3_gateway:
  enabled: false
  host: 0.0.0.0
  port: 39000
  max_connections: 1024
  max_body_size_mb: 5120
  require_sigv4: true
  allow_path_style: true
  allow_virtual_hosted_style: true
  default_bucket_mode: local
  default_api_exclusive: true
  default_remote_sync_strategy: cache
  default_remote_conflict_policy: keep_local
  multipart:
    part_dir: ""
    min_part_size_mb: 5
    abort_after_days: 7
```

Use `vh s3-gateway enable` or `vh s3-gateway disable` to update config and restart only `S3GatewayService`.

## Credentials

Inbound gateway credentials are independent from upstream S3 provider API keys. Do not reuse provider keys for clients.

```bash
vh s3-gateway creds create laptop --json
vh s3-gateway creds list
vh s3-gateway creds revoke VH...
```

The secret access key is printed only when the credential is created. It is encrypted at rest using the gateway TPM key and decrypted only for SigV4 verification.

Gateway credentials have an effective principal user and an additional credential scope. Scope is a restriction layered on top of normal Vaulthalla RBAC:

```text
allowed = principal RBAC allows the action
          and credential scope allows the bucket/vault/action
          and gateway budget preflight allows estimated cost
```

Scope never replaces RBAC. A scoped key cannot reach a vault just because the key lists that vault; the principal user must also have the matching Vaulthalla permission.

Scope modes:

| Scope | Behavior |
| --- | --- |
| `user_access` | The key can access S3 gateway buckets that the principal user can already access through RBAC. |
| `vault_allowlist` | The key can access only listed vaults, with independent list/read/write/delete/admin flags per vault. |
| `global` | Admin-created service credential for all gateway bucket bindings, still subject to admin principal validation and audit fields. |

Examples:

```bash
# Personal full-access key, bounded by the user's normal vault permissions.
vh s3-gateway creds create laptop --scope user-access --json

# Single-bucket backup key.
vh s3-gateway creds create backup \
  --scope vault-allowlist \
  --vault archive \
  --read --write \
  --json

# Multi-vault read-only analytics key.
vh s3-gateway creds create analytics \
  --scope vault-allowlist \
  --vault photos \
  --vault reports \
  --list --read \
  --json

# Global admin service key. Requires admin permission.
vh s3-gateway creds create gateway-ops --scope global --user admin --json
```

Update an existing credential scope:

```bash
vh s3-gateway creds scope backup show
vh s3-gateway creds scope backup set --scope vault-allowlist
vh s3-gateway creds scope backup allow-vault archive --read --write
vh s3-gateway creds scope backup revoke-vault archive
```

## Buckets

Bind an existing vault:

```bash
vh s3-gateway bucket bind photos --vault 12 --mode local
vh s3-gateway bucket list
vh s3-gateway bucket unbind photos
```

Local vaults must be bound as `local`. S3/R2 vaults must be bound as `remote_cache` or `remote_proxy`; the management commands reject mismatched mode/vault-type combinations so remote price-budget checks only run against cloud-backed engines.

Create a local encrypted bucket:

```bash
vh s3-gateway bucket create-local archive
```

Remote-backed API-created buckets should default to smart-cache behavior: `RemotePolicy::Strategy::Cache`, `keep_local` conflicts, balanced request budgets, and API-exclusive ownership unless explicitly overridden.

## Gateway Cost Controls

Remote-backed gateway operations run a price-budget preflight before upstream-costing work. Local-only gateway buckets do not consume remote S3 provider price budgets.

Gateway budget scopes extend the normal global/provider/vault price-budget model:

- `gateway_credential`: monthly cap for one inbound S3 gateway key across all gateway vaults.
- `gateway_credential_vault`: monthly cap for one inbound S3 gateway key on one vault.

Monthly caps are required for gateway credential scopes. Modes match existing price budgets:

| Mode | Behavior |
| --- | --- |
| `off` | Ignore the policy. |
| `report` | Record/report usage but never block. |
| `warn` | Allow and emit warning/notification context. |
| `enforce` | Return S3 XML `AccessDenied` with HTTP 403 when the monthly cap would be exceeded. |

Examples:

```bash
# Monthly spend cap per key. Key-wide caps are admin-managed.
vh s3-gateway budget set-key backup \
  --monthly 5 \
  --mode enforce \
  --currency USD

# Monthly spend cap for one key/vault pair. Vault owners/admins can manage these for vaults they control.
vh s3-gateway budget set-key-vault backup \
  --vault archive \
  --monthly 2 \
  --mode enforce \
  --currency USD

vh s3-gateway budget status --key backup
vh s3-gateway budget ledger --key backup --limit 50
```

When a remote-backed operation is denied, clients receive a normal S3-compatible error response with code `AccessDenied` and a message beginning `S3 gateway price budget would be exceeded`.

## Web Console

The web console includes an `S3 Gateway` admin page. It shows service readiness, request counters, credentials, scope rows, bucket bindings, per-key budgets, per-key/vault budgets, recent ledger rows, and AWS CLI / MinIO client setup snippets. The secret key is shown only immediately after credential creation.

## Client Examples

AWS CLI:

```bash
aws configure set aws_access_key_id VH...
aws configure set aws_secret_access_key ...
aws configure set default.region us-east-1

aws --endpoint-url http://127.0.0.1:39000 s3api list-buckets
aws --endpoint-url http://127.0.0.1:39000 s3 cp ./file.txt s3://archive/file.txt
aws --endpoint-url http://127.0.0.1:39000 s3 rm s3://archive/file.txt
```

MinIO client:

```bash
mc alias set vh http://127.0.0.1:39000 VH... ...
mc mb vh/archive
mc cp ./file.txt vh/archive/file.txt
mc ls vh/archive
mc rm vh/archive/file.txt
```

## Delete Semantics

The gateway currently behaves like unversioned S3. There is no S3 object versioning, object lock, legal hold, MFA delete, or Vaulthalla undo-delete integration for gateway deletes.

For remote-backed buckets, S3 DELETE operations delete upstream first. Only after the upstream S3/R2 delete succeeds does the gateway remove Vaulthalla state: gateway object metadata, remote object index rows, remote manifests, local backing or cache files, and fs cache path/inode/id mappings. If upstream delete fails, local state is retained unless the provider confirms the object is already gone.

This means gateway deletes are destructive for remote-backed buckets today. Plan recovery around upstream bucket versioning or backups until Vaulthalla gateway versioning is implemented.

## Limitations

- No IAM or bucket policy emulation.
- No ACL implementation.
- No object versioning or undo delete.
- No lifecycle rules.
- No object lock, legal hold, or MFA delete.
- Large PUT and multipart bodies are accepted by the gateway path, but operators should still size `max_body_size_mb`, multipart part directories, and request budgets for the workload.
- Remote-backed listing uses Vaulthalla known state and remote indexes; it does not perform unbounded upstream listing on every request.
- Gateway price estimates use the configured provider pricing catalog. They are conservative estimates for preflight/ledger control, not a provider invoice reconciliation system.
