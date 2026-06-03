---
title: Vaulthalla User Documentation
navTitle: Home
description: End-user and operator documentation for installing, configuring, operating, syncing, backing up, and troubleshooting Vaulthalla.
order: 1
status: published
tags:
  - overview
  - operator
---

# Vaulthalla User Documentation

Vaulthalla is a Linux-native, self-hosted vault platform with a C++ daemon, a FUSE filesystem surface, a local CLI, a web console, PostgreSQL-backed metadata, TPM-aware secret handling, role-based access control, and local or S3-compatible vault storage.

These docs focus on using and operating Vaulthalla. They cover installation, first-run setup, CLI and web workflows, vault creation, encryption, sync, cost controls, backup planning, sharing, and troubleshooting.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Start Here

:::cards{columns="3" cardTheme="muted"}
:::card[Install Vaulthalla]{href="/getting-started/installation" linkScope="title"}
Install from the APT repository, choose a lean or interactive profile, and understand what the package creates.
:::

:::card[First Run]{href="/getting-started/first-run" linkScope="title"}
Bootstrap the database, claim the CLI admin Linux UID, configure Nginx, and check service health.
:::

:::card[Web Console]{href="/getting-started/web-console" linkScope="title"}
Open the browser interface and learn how the dashboard, filesystem, vault, user, cost, and admin pages fit together.
:::
:::

## Daily Operation

:::cards{columns="3" cardTheme="muted"}
:::card[CLI Guide]{href="/cli" linkScope="title"}
Use `vh` for status checks, setup, vaults, users, roles, API keys, secrets, sync, pricing budgets, and email checks.
:::

:::card[Vaults]{href="/vaults" linkScope="title"}
Understand local vaults, S3/R2 vaults, the FUSE mount, upstream encryption, and vault-level access control.
:::

:::card[Sync]{href="/vaults/sync" linkScope="title"}
Choose cache, sync, or mirror behavior, run dry-runs, import S3 Inventory, ingest events, and reconcile remote indexes.
:::

:::card[S3 Gateway]{href="/s3-gateway" linkScope="title"}
Expose Vaulthalla vaults through an S3-compatible endpoint for AWS CLI, rclone, MinIO mc, SDKs, and backup tools.
:::
:::

## Safety And Recovery

:::cards{columns="3" cardTheme="muted"}
:::card[Encryption]{href="/vaults/encryption" linkScope="title"}
Understand TPM-backed master keys, per-vault AES keys, upstream S3 encryption, key rotation, and export boundaries.
:::

:::card[Backup And Recovery]{href="/vaults/backup-and-recovery" linkScope="title"}
Build a usable disaster recovery set from PostgreSQL, Vaulthalla state, config, key exports, and secret exports.
:::

:::card[Cost Control]{href="/cost-control" linkScope="title"}
Use request budgets and price budgets to keep S3/R2 operations bounded before sync work runs.
:::
:::

## Administration

Use [Users, Groups, And Roles](/admin/users-groups-roles) to grant access, [S3 Gateway Administration](/admin/s3-gateway) to manage gateway service, credentials, buckets, and budgets, [Operator Emails](/admin/operator-emails) to configure notifications, and [Sharing](/sharing) to issue controlled public or email-validated links. For the first-class S3-compatible protocol guide, start with [S3 Gateway](/s3-gateway).

When something breaks, start with [Install Troubleshooting](/troubleshooting/install-troubleshooting) for package and service issues, then use [General Troubleshooting](/troubleshooting/general-troubleshooting) for CLI, web, vault, sync, encryption, and cost-control symptoms.

## Scope

These pages describe the shipped operator surfaces: `vh`, the web console, the package lifecycle, systemd services, the FUSE mount, PostgreSQL state, TPM or `swtpm` key protection, and S3-compatible storage behavior. Contributor-only docs remain available under [Contributors](/contributors/README), but they are not the primary path for operators.
