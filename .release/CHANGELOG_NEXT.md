<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
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
