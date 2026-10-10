<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Current PDFium and libpqxx 8, with sturdier database connections

This release moves Vaulthalla onto the two libraries it builds for itself, now published only on apt.vaulthalla.sh:
PDFium tracking Chrome Stable (Chromium 155) and libpqxx 8. apt.vaulthalla.sh is now a required package source;
the upgrade pulls in the new PDFium runtime (`libpdfium8059`) and no longer needs a libpqxx runtime package.

### Upgrading

- Upgrade with `sudo apt update && sudo apt upgrade` (or `apt full-upgrade`, or `apt install vaulthalla`). Plain
  `apt-get upgrade` keeps Vaulthalla back because the release adds a new dependency (`libpdfium8059`).
- `libpdfium-2025` and `libpqxx-7.10` are no longer needed and `apt autoremove` offers to remove them. They are no
  longer published, so keep them until you are sure you won't roll back to an earlier release.
- libpqxx 8 requires PostgreSQL 11 or newer; a remote database server older than that is refused at startup.
- An IPv6 `database.host` may be written with or without brackets (`::1` or `[::1]`).

### PDF previews

- PDF previews and thumbnails render with current PDFium. Documents that use predefined CJK character maps now
  render their text; the previous build left it blank.
- Pages render straight into the preview image: about half the memory per page and noticeably faster renders, with
  identical output.

### Database connections

- A database server or network that stops answering without closing the connection is now detected within about a
  minute (TCP keepalives), and the connection is replaced instead of a request waiting for the kernel's two-hour
  timeout.
- A connection that libpqxx reports as unusable after an error (for example a COMMIT whose outcome is unknown) is
  replaced before it can serve another request.
- Vaulthalla's sessions show up as `vaulthalla` in `pg_stat_activity`.
