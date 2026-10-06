<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Key rotation: finish only when every file rotated and a re-query of rows on older key versions is empty (failed files no longer orphaned on a dropped key); per-file sidecar (`<backing>.vh-rotate`, fsynced) + compare-and-set IV commit + rename, with authentication-based crash recovery each pass and at startup; Cache-mode local copies rewritten (inverted check fixed), remote-only files never written locally; empty/IV-less files excluded and one failure no longer aborts its range; single-file rotation no longer divides by zero; `createFile` overwrite replaces ciphertext atomically (temp + fsync + rename + dir fsync).
