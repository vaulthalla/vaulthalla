---
title: S3 Gateway Setup
description: Enable the Vaulthalla S3 Gateway service and choose direct listener or managed Nginx S3-domain endpoints for downstream S3 clients.
order: 10
status: published
tags:
  - s3-gateway
  - setup
  - nginx
  - operator
---

# S3 Gateway Setup

Use this page to enable the S3 Gateway service and choose how downstream S3 clients reach Vaulthalla.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Service Commands

The gateway runtime service is named `S3GatewayService`. Enabling or disabling it updates Vaulthalla config and restarts only the gateway service.

```bash
vh s3-gateway status
vh s3-gateway enable
vh s3-gateway disable
```

`vh s3-gateway status` reports the configured endpoint, readiness, and request counters. Disabling the service does not remove gateway credentials, bucket bindings, or budget policies.

## Config

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
    min_part_size_mb: 5
    abort_after_days: 7
  synthetic_local_request_cost_usd:
    list: "0.00000001"
    head: "0.00000001"
    get: "0.00000001"
    put: "0.00000001"
    delete: "0.00000001"
    copy: "0.00000001"
    downloaded_gb: "0.00000000"
    uploaded_gb: "0.00000000"
```

`synthetic_local_request_cost_usd` is used only when a gateway credential has `enforce_budget_for_local_requests` enabled. It gives local/cache requests a tiny nominal cost for gateway key budgets without creating provider, vault, or global upstream usage.

## Direct Listener

Use the direct listener for local clients and development:

```bash
aws configure set s3.addressing_style path
aws --endpoint-url http://127.0.0.1:39000 s3api list-buckets
```

The default local URL is:

```text
http://127.0.0.1:39000
```

Keep SigV4 enabled for real client validation. Downstream clients should generally use path-style addressing, especially when they share configuration with the public S3-domain endpoint.

## Managed Nginx S3-Domain Endpoint

Managed Nginx can publish a dedicated public S3 hostname:

```bash
sudo vh setup nginx \
  --domain vaulthalla.example.com \
  --s3-domain s3.vaulthalla.example.com \
  --certbot
```

For local or lab hosts using Cloudflare DNS-01 certificates:

```bash
sudo vh setup nginx \
  --domain vaulthalla.dev \
  --s3-domain s3.vaulthalla.dev \
  --certbot-dns-cloudflare \
  --cloudflare-credentials /etc/vaulthalla/certbot/cloudflare.ini
```

The S3 domain proxies requests to the direct gateway listener and preserves the signed URI. It marks requests as path-style-only so the router does not interpret `s3.vaulthalla.example.com` as a virtual-hosted bucket.

## Path-Style And SigV4

Use path-style URLs for downstream clients:

```text
https://s3.vaulthalla.example.com/archive/reports/report.pdf
```

That shape maps `archive` to the gateway bucket and `reports/report.pdf` to the object key. SigV4 signs the path sent by the client, so reverse proxies must preserve the URI and host behavior expected by the managed S3-domain configuration.

Virtual-hosted style may be accepted by the direct listener when enabled, but path-style is the operational default for public reverse-proxy mode.

## Web Console Workflow

Use the browser workflow for interactive setup:

1. Navigate to Admin -> S3 Gateway.
2. Check service readiness and endpoint information.
3. Create a gateway credential.
4. Copy the secret immediately; the secret access key is shown only once.
5. Choose `user_access`, `vault_allowlist`, or `global` scope.
6. Add vault role assignments and optional path overrides when using `vault_allowlist`.
7. Create or bind a gateway bucket.
8. Set gateway key or key/vault budgets.
9. Copy AWS CLI or MinIO client snippets.
10. Use budget status and ledger views to troubleshoot `AccessDenied` or `SlowDown` responses.

## CLI Workflow

Use the CLI for automation and host-local checks:

```bash
vh s3-gateway status
vh s3-gateway enable
vh s3-gateway disable
```

Credentials:

```bash
vh s3-gateway creds create laptop --json
vh s3-gateway creds list
vh s3-gateway creds revoke VH...
vh s3-gateway creds scope backup show
vh s3-gateway creds scope backup set --scope vault-allowlist
vh s3-gateway creds role assign backup --vault archive --role contributor
vh s3-gateway creds role override add backup --vault archive --pattern "/private/*" --permission download --effect deny
vh s3-gateway creds role override list backup --vault archive
vh s3-gateway creds role revoke backup --vault archive
```

Legacy boolean scope flags and `creds scope allow-vault` are compatibility shorthand. Gateway authorization uses vault roles and path overrides.

Buckets:

```bash
vh s3-gateway bucket list
vh s3-gateway bucket bind photos --vault 12 --mode local
vh s3-gateway bucket create-local archive
vh s3-gateway bucket create-remote-cache edge --api-key r2-main --upstream-bucket origin --encrypt
vh s3-gateway bucket backfill archive
```

Budgets:

```bash
vh s3-gateway budget set-key backup --monthly 5 --mode enforce --currency USD
vh s3-gateway budget set-key-vault backup --vault archive --monthly 2 --mode enforce
vh s3-gateway budget list --key backup
vh s3-gateway budget disable-key backup
vh s3-gateway budget disable-key-vault backup --vault archive
vh s3-gateway budget status --key backup
vh s3-gateway budget ledger --key backup --limit 50
```
