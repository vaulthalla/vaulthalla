<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- Build against apt.vaulthalla.sh's libpqxx-vh-dev (libpqxx 8.0.2, static; pkg-config `libpqxx-vh`) instead of
  libpqxx-dev 7.10; the package no longer depends on a libpqxx runtime, only libpq5.
  - Models and mappers take `pqxx::row_ref`/`pqxx::field_ref` by value (`db/Fwd.hpp`); rows come from named
    results (`one_row_ref()`), and `db/Rows.hpp` replaces the hand-written result-to-model loops.
  - Enum parameters bind their SQL text; libpqxx is no longer in the -O0 precompiled header.
- Build against libpdfium-dev 155.8059 (Chromium-tracking PDFium; runtime libpdfium8059); meson requires
  `pdfium >=155.8059, <1000` so the retired 20250629 snapshot can't be picked up.
  - PDF pages render directly into the RGB raster (24bpp, `FPDF_REVERSE_BYTE_ORDER`) through upstream's scoped
    handles; one `PdfiumLibrary` owns init/teardown (fixes an uninitialized `m_pPlatform` in the old config).
- Database connections use libpq keyword parameters, TCP keepalives, `tcp_user_timeout` and
  `application_name=vaulthalla`; connections poisoned per `pqxx::failure::poisons_connection()` are replaced; a bracketed
  IPv6 `database.host` is passed to libpq bare.
- bin/setup/install_deps.sh configures apt.vaulthalla.sh with a signed-by keyring and installs the Chromium-versioned
  libpdfium-dev over the old date-versioned one.
