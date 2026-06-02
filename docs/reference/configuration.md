---
title: Configuration
description: Reference for the main Vaulthalla configuration areas operators commonly need to understand.
order: 710
status: published
tags:
  - reference
  - configuration
  - operations
---

# Configuration

The main configuration file is:

```text
/etc/vaulthalla/config.yaml
```

Most operators should use CLI setup commands for lifecycle changes and edit `config.yaml` only for settings that are intentionally file-based.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Service Endpoints

Common sections:

```yaml
websocket_server:
  enabled: true
  host: 127.0.0.1
  port: 36969

http_preview_server:
  enabled: true
  host: 127.0.0.1
  port: 36970
```

The web service itself is managed by `vaulthalla-web.service` and normally listens on localhost for Nginx proxying.

The S3-compatible gateway is a separate runtime service and is disabled by default:

```yaml
s3_gateway:
  enabled: false
  host: 0.0.0.0
  port: 39000
  require_sigv4: true
  allow_path_style: true
  allow_virtual_hosted_style: true
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

See [S3 Gateway](/admin/s3-gateway) before enabling it on a network interface.

`require_sigv4` should stay enabled outside development. When it is disabled, the gateway accepts a development-only auth context only if `dev.enabled` is true or the configured host is loopback. Production listeners should use real gateway credentials with explicit scope and normal Vaulthalla RBAC.

Clients can use the direct listener, or the managed Nginx path endpoint:

```bash
aws --endpoint-url http://127.0.0.1:39000 s3api list-buckets
aws configure set s3.addressing_style path
aws --endpoint-url https://vaulthalla.example.com/api/s3 s3api list-buckets
```

The `/api/s3` endpoint is path-style reverse-proxy mode. SigV4 signs `/api/s3/...`; Nginx preserves the signed URI, and the gateway strips the prefix only after authentication. `s3_gateway.multipart.part_dir` is no longer a valid setting; multipart parts use a Vaulthalla-owned hidden backing path with opaque per-upload directories.

`synthetic_local_request_cost_usd` is used only for S3 gateway credentials with local/cache budget enforcement enabled. It gives pure local buckets, metadata hits, cache hits, and sync-deferred gateway writes/deletes a nominal gateway-local cost for `gateway_credential` budgets without touching provider/vault/global upstream budgets.

## Database

The database section controls PostgreSQL connection shape:

```yaml
database:
  host: localhost
  port: 5432
  name: vaulthalla
  user: vaulthalla
  pool_size: 10
```

Use these setup commands rather than editing secrets directly:

```bash
sudo vh setup db
sudo vh setup remote-db --host <host> --user <user> --database <name> --password-file <path>
vh secret set db-password /path/to/password-file
```

## Authentication

Authentication settings include access-token and refresh-token lifetimes:

```yaml
auth:
  access_token_expiry_minutes: 15
  refresh_token_expiry_days: 30
```

Changing token lifetimes affects web and API sessions. Use short access tokens and rotate secrets deliberately.

## Sharing

Sharing settings control whether share features and public links are enabled:

```yaml
sharing:
  enabled: true
  enable_public_links: true
```

Use [Sharing](/sharing) for operational guidance.

## Pricing And Storage Rates

Pricing configuration controls whether price-budget checks can estimate provider costs:

```yaml
pricing:
  enabled: true

storage_rates_api:
  remote_refresh_enabled: false
  fail_open: true
```

Remote refresh is opt-in. If you enforce price budgets, review catalog freshness policy and verification requirements in [Price Budgets](/cost-control/price-budgets).

S3 gateway per-key budgets use the same price-budget service and pricing catalogs. Remote-backed gateway operations evaluate global, provider, vault, gateway credential, and gateway credential/vault policies before upstream-costing work. Local-only gateway buckets do not consume remote provider price budgets.

## Sync Audit Retention

Sync event audit settings control how long sync event details are retained:

```yaml
sync:
  event_audit_retention_days: 30
  event_audit_max_entries: 10000
```

Short retention reduces database growth but can remove useful sync troubleshooting context.

## Stats Snapshots

Stats snapshot settings control dashboard trend collection:

```yaml
stats_snapshots:
  enabled: true
  runtime_interval_seconds: 60
  vault_interval_seconds: 300
  retention_days: 30
```

Dashboard backup/recovery readiness indicators should be treated as policy/status signals, not proof that backup work has run.

## Cache And Preview Settings

Cache and thumbnail settings affect web preview behavior and local cache size:

```yaml
caching:
  max_size_mb: 1024
```

If previews fail but downloads work, check preview service health, file type support, and cache capacity.

## Operator Email

Operator email sections configure provider and recipients:

```yaml
email:
  enabled: true
  provider: resend
  from: "Vaulthalla <ops@example.com>"
  base_url: "https://vault.example.com"

operator_emails:
  enabled: true
```

See [Operator Emails](/admin/operator-emails).

## Development Settings

Development settings are not production controls:

```yaml
dev:
  enabled: false
```

Do not enable development behavior on production hosts unless a maintainer-specific procedure requires it.
