<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Rich previews

### Optional media helper (`vaulthalla-preview-media`)

- A new optional helper probes video and audio files (container, codecs, duration, and whether Chrome, Firefox
  and Safari can play them directly), renders poster frames, and transcodes to browser-safe H.264/AAC as
  fragmented MP4 or HLS. It runs out of process, reads plaintext only through the daemon's range channel (no
  plaintext temp files, and MP4 files with the index at the end are read in place), and sandboxes itself before
  touching input. Output size, duration, dimensions and wall time are capped.
- Hardware encoding (`preview.media.hwaccel`: VAAPI, Quick Sync, NVENC) is probed and falls back to software on
  any failure. Hardware paths have not yet been validated on real GPUs; software (libx264) is the tested path.
