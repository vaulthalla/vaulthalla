---
title: Runtime Paths
description: Reference for Vaulthalla configuration, runtime, state, log, mount, SQL, web, Nginx, and TPM paths.
order: 700
status: published
tags:
  - reference
  - paths
  - operations
---

# Runtime Paths

Use this reference when backing up, troubleshooting, or verifying package layout.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Core Paths

| Path | Purpose |
| --- | --- |
| `/etc/vaulthalla/config.yaml` | Main runtime configuration. |
| `/run/vaulthalla` | Runtime sockets and transient secrets. |
| `/var/lib/vaulthalla` | Vaulthalla state, local vault bodies, cache/index state, and sealed blobs. |
| `/var/log/vaulthalla` | Log location where file logging is configured. |
| `/mnt/vaulthalla` | FUSE filesystem mount. |
| `/usr/share/vaulthalla/psql` | Packaged SQL schema and migration assets. |
| `/usr/share/vaulthalla-web` | Packaged web runtime. |
| `/var/cache/vaulthalla-web` | Web runtime cache. |

## Services

| Service | Purpose |
| --- | --- |
| `vaulthalla.service` | Core daemon. |
| `vaulthalla-cli.socket` | Local CLI socket activation. |
| `vaulthalla-cli.service` | CLI socket service. |
| `vaulthalla-web.service` | Web console runtime. |
| `vaulthalla-swtpm.service` | Managed software TPM fallback. |

## Network Defaults

| Listener | Default |
| --- | --- |
| Web runtime | `127.0.0.1:36968` |
| WebSocket server | `127.0.0.1:36969` |
| Preview HTTP server | `127.0.0.1:36970` |
| Software TPM | `127.0.0.1:2321` and `127.0.0.1:2322` |

These listeners are intended for local service wiring and Nginx proxying, not direct public exposure.

## Nginx Paths

| Path | Purpose |
| --- | --- |
| `/usr/share/vaulthalla/nginx/vaulthalla.conf` | Packaged Nginx template. |
| `/etc/nginx/sites-available/vaulthalla` | Managed site configuration. |
| `/etc/nginx/sites-enabled/vaulthalla` | Enabled site symlink. |
| `/var/lib/vaulthalla/nginx_site_managed` | Marker for package-managed Nginx state. |

Use `sudo vh setup nginx` and `sudo vh teardown nginx` instead of editing managed state by hand when possible.

## TPM Paths

| Path | Purpose |
| --- | --- |
| `/dev/tpmrm0` | Preferred hardware TPM resource manager device. |
| `/dev/tpm0` | Hardware TPM fallback device. |
| `/var/lib/swtpm/vaulthalla` | Software TPM state. |

Sealed key blobs are stored in Vaulthalla state. Treat TPM and sealed key state as sensitive backup material.

## CLI Socket

The CLI connects to:

```text
/run/vaulthalla/cli.sock
```

If `vh` fails with a permission error:

```bash
ls -l /run/vaulthalla/cli.sock
id
getent group vaulthalla
systemctl status vaulthalla-cli.socket
```
