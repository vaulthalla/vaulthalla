<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- lifecycle: `vh setup nginx --certbot-dns-cloudflare <credentials>` takes the Certbot
  Cloudflare credentials path as its value; `--cloudflare-credentials <path>` is kept as
  the older spelling, enables DNS-01 mode on its own, and conflicting paths are refused.
- usage/docs: `vh setup nginx` help and the S3 gateway guide document the credentials file
  (`dns_cloudflare_api_token`, token scope, 0600 creation, keep it for renewals) and why
  `--s3-domain` requires DNS-01; the guide's invalid `--s3-domain --certbot` example is gone.
- cli: `--help`/`-h` on lifecycle commands (`setup db|remote-db|nginx`, `teardown db|nginx`)
  is answered from the daemon's usage book without sudo; root falls back to the lifecycle
  utility's argparse help only when the daemon socket is unreachable.
