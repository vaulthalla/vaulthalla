<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- postinst/prerm: routine step output is log-only (/var/log/vaulthalla-package.log, root:adm 0640, symlink-refusing,
  rotated at 1 MiB, removed on purge; systemctl transition output captured there too). A clean upgrade prints one
  status line plus an Action line only for pending operator work; warnings stay on the terminal; hard failures
  (core not active after restart, CLI socket missing, TPM deferred, DB bootstrap failed/deferred, config missing)
  print an ERROR and the full summary; fresh/reinstall keep the full summary. VH_PACKAGE_VERBOSE=1 prints everything.
