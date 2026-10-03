<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->

## Runtime
- Fix the sync controller spinning one CPU core at 100% whenever the next
  sync was scheduled in the future (always, once a vault had synced): it now
  sleeps until the earliest sync is due, a sync is queued, or the service
  stops.
