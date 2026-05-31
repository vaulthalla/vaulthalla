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

## Buckets

Bind an existing vault:

```bash
vh s3-gateway bucket bind photos --vault 12 --mode local
vh s3-gateway bucket list
vh s3-gateway bucket unbind photos
```

Create a local encrypted bucket:

```bash
vh s3-gateway bucket create-local archive
```

Remote-backed API-created buckets should default to smart-cache behavior: `RemotePolicy::Strategy::Cache`, `keep_local` conflicts, balanced request budgets, and API-exclusive ownership unless explicitly overridden.

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
