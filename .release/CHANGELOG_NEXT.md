<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
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
