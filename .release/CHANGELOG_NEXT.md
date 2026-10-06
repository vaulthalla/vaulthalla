<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- preview-media: add the optional `vaulthalla-preview-media` helper (meson feature `preview_media`, links
  libavformat/libavcodec/libswscale/libswresample; never linked by the daemon) with `probe`, `poster`,
  `transcode` (fragmented MP4, H.264 + AAC stereo), `hls` (framed fMP4 segments, VOD playlist last) and
  `capabilities`, over the derive-seam range-pull protocol with a custom seekable AVIOContext.
  - Limits: demuxer whitelist, nested opens refused, probesize/analyzeduration caps, dimension/stream/channel
    caps, deadline interrupt, --max-output-bytes; corrupt input exits 2 (invalid_input), caps exit 3.
  - `--hwaccel auto|software|vaapi|qsv|nvenc`: devices are created before the sandbox and every hardware path
    falls back to libx264; hardware encoding is unvalidated.
