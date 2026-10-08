<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
## Security
- HTTP previews (`/preview`) enforce vault RBAC like downloads; share links distinguish Preview from Download:
  a preview-only link gets rendered previews (images, PDF pages) but never original bytes, conversions of
  downloadable kinds, or the text/media/3D lanes. Callers who cannot read a vault get the same 404 for existing
  and missing paths.
- No plaintext on disk for previews: PDF/image rendering decrypts in memory (the decrypt-to-temp-file helper is
  removed), thumbnails/renders are cached encrypted with the vault key, and `/preview` and `/download` responses
  carry `X-Accel-Buffering: no` so nginx never spools decrypted bodies (fresh nginx sites also set
  `proxy_buffering off` and `proxy_max_temp_file_size 0`; existing sites are left alone and rely on the header).
- Hostile-input bounds for in-process rendering: 64 MP source cap (`preview.max_render_pixels`; progressive JPEGs
  half), read from the header before decoding; at most 4 renders at once; PDFium serialized behind one lock;
  failed sources negatively cached per file version.
- Pre-authentication request bodies are refused/bounded; HTTP connections get real read/write deadlines.

## Authentication
- The password dictionary check matched any >=3-letter word as a substring,
  refusing most long random passwords (the longer, the likelier). It now refuses
  only a password that is a dictionary word after trimming leading/trailing
  non-letters. One policy (Validator::passwordPolicyViolation) serves
  registration and password change; passwords of 20+ characters no longer need
  a digit; refusals name the violated rule. Drops a leaked curl handle per
  breach lookup. Regression test: test_password_policy.

## Data safety
- Key rotation: finish only when every file rotated and a re-query of rows on older key versions is empty (failed files no longer orphaned on a dropped key); per-file sidecar (`<backing>.vh-rotate`, fsynced) + compare-and-set IV commit + rename, with authentication-based crash recovery each pass and at startup; Cache-mode local copies rewritten (inverted check fixed), remote-only files never written locally; empty/IV-less files excluded and one failure no longer aborts its range; single-file rotation no longer divides by zero; `createFile` overwrite replaces ciphertext atomically (temp + fsync + rename + dir fsync).

## HTTP and downloads
- One access layer for every HTTP lane (session cookie, share token, RBAC, share scope) and one RFC 9110
  byte-range parser shared with the S3 gateway.
- Streaming responses with GET/HEAD, single Range (206/416), strong ETags per file version, If-None-Match,
  strong-only If-Range, `Content-Disposition` inline/attachment and `nosniff`/sandbox hardening for original bytes.
- New `/download/content` lane for inline original bytes (media seeking, 3D, text); `/download` streams files of
  any size (the 256 MiB in-memory cap is gone; folder ZIPs are still built in memory, two at a time).
- `PUT /upload/text` saves text edits with `If-Match` (412 on conflict, 428 without a validator,
  `preview.text.max_edit_bytes`).
- `storage::PlaintextReader` over AES-256-GCM vault files: positioned reads via CTR after the file version is
  authenticated once (integrity registry keyed by IV, key version and inode identity; strict mode for derivation);
  no change to the on-disk file format.
- Capped thread-per-connection HTTP server (`http_preview_server.max_connections`, 503 + `Retry-After` beyond it).
- Hot path: validated sessions and share principals are cached briefly and invalidated by an RBAC/share policy
  epoch (revocations apply immediately); share access audit rows are coalesced; `max_downloads` is consumed
  atomically in SQL.
- Crypto: OpenSSL AES-256-GCM primitives (streaming encrypt/verify, CTR reads), zeroized key snapshots and a
  thread-safe EncryptionManager.

## Database
- Migration 103: `files.encryption_format`; `cache_index` gains the `derived` type with kind/variant/source
  generation/generator version/artifact IV and key version/status columns, a unique derived identity and an LRU
  index. Legacy thumbnail/file cache rows are dropped (thumbnails regenerate encrypted).

## Runtime
- Crash safety: MIME detection (libmagic) no longer shares an unlocked cookie across threads (concurrent FUSE
  writes/uploads corrupted the heap); the daemon is non-dumpable and logs a backtrace on a fatal signal, so a crash
  exits and restarts instead of hanging its own FUSE mount (and `apt`) in an unfinishable core dump.
- Cloud vaults: reads prefer the local ciphertext copy; the ws share download/preview lanes and S3 gateway GETs no
  longer fetch the object from S3 when it is stored locally, and gateway Range GETs read only the range.
- Remote-only (Cache index-only) files are readable: hydrate-first by default (price-preflighted, request-capped,
  one If-Match GET streamed to an unnamed temp file, whole-message GCM verification, kept as local ciphertext;
  plaintext-upstream objects are sealed on the way in), opt-in metered ranged reads (`preview.media.remote: ranged`,
  unauthenticated per range), or `off`. Concurrent readers share one fetch; refusals are ContentUnavailable.
  - S3 controller: metered streaming GET with signed Range/If-Match and stall timeouts; nested usage captures all
    see each request.
- preview::derive::Runner: runs out-of-process converter helpers from preview.derive.helper_dir
  (/usr/lib/vaulthalla/helpers): fork + exec with setsid, PDEATHSIG, NO_NEW_PRIVS, rlimits (AS, CPU, FSIZE=0,
  NOFILE=64, CORE=0), only fds 0-4, empty environment; plaintext served over a range-pull socketpair (never on
  disk); output cap and wall-clock timeout kill the process group; crashes are reported, never thrown.
- Helpers self-confine with Landlock (read-only system paths, ABI-aware) and a seccomp denylist (sockets, exec,
  process creation, ptrace, signals to other processes, mounts, opens for writing); a helper without Landlock or
  seccomp refuses to run (exit 5 sandbox_unavailable, reported as converter_unavailable and logged once).
- Helper sandbox hardening: seccomp limits prlimit64/setpriority/ioprio_set/sched_set*/move_pages/migrate_pages
  to the helper itself and denies fcntl F_SETOWN/F_SETOWN_EX/F_SETSIG/F_SETLEASE, ioctl FIOSETOWN/SIOCSPGRP, SysV
  IPC and POSIX mqueues (a same-uid helper could lower the daemon's RLIMIT_NOFILE or arm SIGKILL at it); the CAD
  helper denies threads, and the runner SIGKILLs helpers above 64 threads; media hardware devices open after the
  sandbox; the runner reads pipes in bounded chunks per poll (stderr floods no longer starve the wall timeout);
  only root-owned, non-group/world-writable helpers (and directories) run, and preview.derive.helper_dir is no
  longer writable through settings.update or the CLI.
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
- vaulthalla.service: `LimitCORE=0`; the unit documents why mount-namespace hardening and NoNewPrivileges stay
  off (they hide the FUSE mount / break setuid fusermount3). Fresh nginx sites disable proxy buffering on
  `/preview` and `/download`.
- New binary packages vaulthalla-preview-cad (STEP/STP -> GLB, Open CASCADE) and vaulthalla-preview-media
  (libav*), each Depends: vaulthalla (= ${binary:Version}); vaulthalla Suggests both and links neither.
  Build-Depends gain libseccomp-dev, libocct-*-dev and libav*/libswresample-dev/libswscale-dev behind the build
  profiles pkg.vaulthalla.nocad / pkg.vaulthalla.nomedia; meson options preview_cad / preview_media (feature, auto).
- preview-media: add the optional `vaulthalla-preview-media` helper (meson feature `preview_media`, links
  libavformat/libavcodec/libswscale/libswresample; never linked by the daemon) with `probe`, `poster`,
  `transcode` (fragmented MP4, H.264 + AAC stereo), `hls` (framed fMP4 segments, VOD playlist last) and
  `capabilities`, over the derive-seam range-pull protocol with a custom seekable AVIOContext.
  - Limits: demuxer whitelist, nested opens refused, probesize/analyzeduration caps, dimension/stream/channel
    caps, deadline interrupt, --max-output-bytes; corrupt input exits 2 (invalid_input), caps exit 3.
  - `--hwaccel auto|software|vaapi|qsv|nvenc`: devices are created inside the sandbox (GPU allowance) and every
    hardware path falls back to libx264; hardware encoding is unvalidated.

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
- Web: the 3D viewer can no longer be made to fetch a URL chosen by the model: OBJ `mtllib` and MTL texture
  statements are matched the way Babylon tokenises and decodes them (any case/whitespace, CR/U+2028 line starts,
  UTF-16/GB18030) and rewritten to viewer-created object URLs or removed (materials skipped when none resolve), and an
  engine-wide URL gate (Tools.PreprocessUrl, ScriptPreprocessUrl, WebRequest modifiers) only lets Babylon load data:
  URLs and URLs the viewer registered; glTF blob:/absolute URIs are refused. Side-file requests are aborted and object
  URLs revoked when the viewer closes, and a model may pull in at most 256 side files and 512 MiB including itself.
