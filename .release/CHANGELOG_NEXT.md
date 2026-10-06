<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
## Runtime
- preview::derive::Runner: runs out-of-process converter helpers from preview.derive.helper_dir
  (/usr/lib/vaulthalla/helpers): fork + exec with setsid, PDEATHSIG, NO_NEW_PRIVS, rlimits (AS, CPU, FSIZE=0,
  NOFILE=64, CORE=0), only fds 0-4, empty environment; plaintext served over a range-pull socketpair (never on
  disk); output cap and wall-clock timeout kill the process group; crashes are reported, never thrown.
- Helpers self-confine with Landlock (read-only system paths, ABI-aware) and a seccomp denylist (sockets, exec,
  process creation, ptrace, signals to other processes, mounts, opens for writing); a helper without seccomp
  refuses to run.
- New optional config keys preview.{media.*,derive.*,text.max_edit_bytes,max_render_pixels} with defaults.

## Packaging
- New binary packages vaulthalla-preview-cad (STEP/STP -> GLB, Open CASCADE) and vaulthalla-preview-media
  (libav*), each Depends: vaulthalla (= ${binary:Version}); vaulthalla Suggests both and links neither.
  Build-Depends gain libseccomp-dev, libocct-*-dev and libav*/libswscale-dev behind the build profiles
  pkg.vaulthalla.nocad / pkg.vaulthalla.nomedia; meson options preview_cad / preview_media (feature, auto).
