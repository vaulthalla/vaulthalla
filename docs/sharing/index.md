---
title: Sharing
description: Create and manage Vaulthalla public or email-validated share links with scoped file operations.
order: 400
status: published
tags:
  - sharing
  - web
  - access
---

# Sharing

Vaulthalla shares expose selected file or directory access through scoped links. A share can be public or email-validated, and it only grants the operations included in the selected share role.

:::toc[On this page]{depth="3" theme="compact"}
:::

## Share Types

| Access mode | Behavior |
| --- | --- |
| Public | Anyone with the link can use the share within its allowed operations. |
| Email-validated | Recipients verify through email before access. |

Use email validation for sensitive data or when you need recipient-level control.

## Presets

Common presets include:

| Preset | Typical permissions |
| --- | --- |
| Browse + Download | Metadata, list, preview, and download. |
| Download Only | File download without broader browsing. |
| Upload Dropbox | Directory upload-oriented access. |
| Custom Role | Operator-selected operation set. |

Available operation bits include metadata, list, preview, download, upload, mkdir, and overwrite.

## Create A Share

In the web filesystem browser, select a file or directory and open the share action. Choose the access mode, preset or custom role, recipient rules, and expiration or limits where available.

After creation, copy the generated URL immediately.

:::callout{theme="warning" title="Share URLs are shown only at create or rotate time"}
Vaulthalla intentionally displays public share URLs only immediately after creation or rotation. If the URL is lost, rotate the share to generate a new one.
:::

## Manage Shares

Use the Shares page to review active shares, rotate URLs, disable access, or inspect share metadata. Public links should be rotated if they were sent to the wrong recipient or exposed in an untrusted place.

## Share-Mode Filesystem

When a recipient opens a share link, the web UI switches into share mode. Only the share's allowed operations are available. For example, a download-only share should not expose upload or mkdir actions.

Preview availability depends on file type, preview service health, and the share role. If preview is not available but download is permitted, recipients may need to download the file.

## Operator Email Dependency

Email-validated shares depend on working operator email configuration. Before using email-validated access for production workflows, verify:

```bash
vh email doctor
vh email test --dry-run
vh email test --send --to ops@example.com
```

See [Operator Emails](/admin/operator-emails).

## Security Practices

- Prefer email validation for sensitive shares.
- Scope shares to the narrowest file or directory.
- Use download-only links when browsing is not needed.
- Rotate public URLs after accidental exposure.
- Disable old shares instead of leaving stale access in place.
- Review vault roles before creating a share on behalf of another user.
