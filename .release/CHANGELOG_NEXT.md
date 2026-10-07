<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
## Data safety
- Key rotation: finish only when every file rotated and a re-query of rows on older key versions is empty (failed files no longer orphaned on a dropped key); per-file sidecar (`<backing>.vh-rotate`, fsynced) + compare-and-set IV commit + rename, with authentication-based crash recovery each pass and at startup; Cache-mode local copies rewritten (inverted check fixed), remote-only files never written locally; empty/IV-less files excluded and one failure no longer aborts its range; single-file rotation no longer divides by zero; `createFile` overwrite replaces ciphertext atomically (temp + fsync + rename + dir fsync).

## Runtime
- preview::derive::Runner: runs out-of-process converter helpers from preview.derive.helper_dir
  (/usr/lib/vaulthalla/helpers): fork + exec with setsid, PDEATHSIG, NO_NEW_PRIVS, rlimits (AS, CPU, FSIZE=0,
  NOFILE=64, CORE=0), only fds 0-4, empty environment; plaintext served over a range-pull socketpair (never on
  disk); output cap and wall-clock timeout kill the process group; crashes are reported, never thrown.
- Helpers self-confine with Landlock (read-only system paths, ABI-aware) and a seccomp denylist (sockets, exec,
  process creation, ptrace, signals to other processes, mounts, opens for writing); a helper without seccomp
  refuses to run.
- New optional config keys preview.{media.*,derive.*,text.max_edit_bytes,max_render_pixels} with defaults.
- preview::derive::Queue: bounded (preview.derive.max_queue), deduplicating derive queue with
  preview.derive.max_concurrency workers in front of the helpers (model-glb, poster-jpg, probe-json,
  transcode-h264-{480,720,1080}); results are sealed into the derived-artifact cache, deterministic failures
  negatively cached, transient ones (remote/budget refusals, I/O) never; a source rewritten mid-job caches
  nothing; missing helper packages report converter_unavailable; daemon stop kills running helpers.
- Derived-artifact cache lifecycle: every start sweeps legacy plaintext thumbnails and orphaned artifacts per
  vault; the janitor evicts to caching.max_size_mb / caching.thumbnails.expiry_days every 15 minutes; file
  delete/trash/purge (web, CLI, FUSE, S3 gateway) and vault removal drop the file's artifacts; finished key
  rotations drop artifacts sealed under the retired key. preview.media.integrity/remote now set the reader
  defaults at startup. The path-keyed thumbnail move/copy/purge helpers are removed.

## Packaging
- New binary packages vaulthalla-preview-cad (STEP/STP -> GLB, Open CASCADE) and vaulthalla-preview-media
  (libav*), each Depends: vaulthalla (= ${binary:Version}); vaulthalla Suggests both and links neither.
  Build-Depends gain libseccomp-dev, libocct-*-dev and libav*/libswscale-dev behind the build profiles
  pkg.vaulthalla.nocad / pkg.vaulthalla.nomedia; meson options preview_cad / preview_media (feature, auto).

- preview-media: add the optional `vaulthalla-preview-media` helper (meson feature `preview_media`, links
  libavformat/libavcodec/libswscale/libswresample; never linked by the daemon) with `probe`, `poster`,
  `transcode` (fragmented MP4, H.264 + AAC stereo), `hls` (framed fMP4 segments, VOD playlist last) and
  `capabilities`, over the derive-seam range-pull protocol with a custom seekable AVIOContext.
  - Limits: demuxer whitelist, nested opens refused, probesize/analyzeduration caps, dimension/stream/channel
    caps, deadline interrupt, --max-output-bytes; corrupt input exits 2 (invalid_input), caps exit 3.
  - `--hwaccel auto|software|vaapi|qsv|nvenc`: devices are created before the sandbox and every hardware path
    falls back to libx264; hardware encoding is unvalidated.

## Web console
- Web console: render file previews from the server's preview plan (`preview` on file entries; MIME
  fallback only for older daemons), gated by the plan's capability (preview vs download).
  - Renderer registry in the lazy preview sheet; PDF/media/text/3D renderers are nested next/dynamic
    chunks (CodeMirror 6, react-markdown and the 3D engine never enter route first-load JS).
  - PDF paging via /preview page=N + X-Vaulthalla-Page-Count; native <video>/<audio> over ranged
    /download/content with a codec fallback and /preview/derived transcode polling; SVG/WebP originals.
  - Text/Markdown view (2 MiB, UTF-8 only, no raw HTML) and console editing saved with
    PUT /upload/text + If-Match (412 conflict dialog, 403/413/428 handled).
  - 3D: model bytes from /download/content (512 MiB cap), STEP via /preview/derived kind=model-glb,
    glTF side files resolved to sibling paths without escaping the vault or share root.
  - Thumbnails only for plans with thumbnail=true; first re-poll at 250 ms. Download preflight is a
    HEAD instead of a full GET.
  - Playwright suite tests/e2e/preview.spec.ts with generated fixtures.
- Web: 3D model viewer (web/src/features/files/preview/ModelViewer.tsx) on Babylon.js 9.29.0
  (@babylonjs/core + @babylonjs/loaders, granular imports, per-format loaders and glTF extensions loaded on
  demand) for GLB/glTF/STL/OBJ: orbit/pan/zoom, fit/reset, grid, wireframe, stats; pre-scan caps (20M triangles,
  30M vertices, 512 MiB of OBJ/ASCII-STL text, 64 MiB glTF JSON, 4096 px textures); glTF external buffers/textures
  and OBJ .mtl/textures through a resolveResource callback; absolute/other-host references never fetched;
  WebGL-missing and context-loss states.
- Web: Draco (draco3dgltf 1.5.7) and meshopt (meshoptimizer 1.3.0) decoders bundled under /_next/static instead of
  cdn.babylonjs.com; KTX2/Basis not shipped (fallback image used, required-KTX2 models refused); Babylon CDN script
  and asset URLs redirected to a same-origin dead path.
- Web: bin/check-budgets.mjs fails when Babylon code reaches any route's first load and budgets the model viewer's
  lazy chunks (perf-budgets.json lazy.modelViewer 325 KB on open, lazy.modelViewerReachable 1840 KB).
