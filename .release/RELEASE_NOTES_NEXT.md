<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Rich previews, streaming downloads and safer key rotation

## Security fixes in previews and share links

- **Previews now follow vault permissions.** Rendered previews require the same vault access as downloads, and
  someone who cannot read a vault can no longer tell which paths exist in it.
- **Preview-only share links stay preview-only.** They show rendered images and PDF pages, but never hand out a
  file's original bytes (or a converted copy of something only a download link should get).
- **Nothing decrypted is left on disk for previews.** PDF and image previews render in memory, cached previews are
  encrypted with the vault key (old unencrypted thumbnails are deleted on upgrade and regenerate), and the daemon
  tells nginx not to buffer decrypted responses to disk. New installs also disable proxy buffering in the nginx
  site; an existing site file is not modified and relies on that header.
- **Hostile images and PDFs are bounded:** oversized images are refused from their header before decoding
  (`preview.max_render_pixels`, 64 MP by default), at most four renders run at once, and a file that fails to
  render is not retried until it changes.
- **A crash restarts the daemon instead of hanging the mount.** A race in file-type detection could crash the
  daemon under concurrent uploads; it is fixed, and a crash now exits and restarts cleanly rather than leaving
  `/mnt/vaulthalla` (and `apt`) stuck.

## Downloads stream, with seeking and no size cap

Downloads are streamed straight from the encrypted vault instead of being decrypted into memory first: the first
byte arrives immediately, memory use no longer grows with file size, and the old 256 MiB download limit is gone
(folder downloads as ZIP are still limited). Range requests, `HEAD`, ETags and conditional requests work, so
video and audio seek, resumable download tools resume, and the browser can revalidate instead of refetching.
Integrity is still checked: each file version is authenticated once before ranged reads are served from it.
Busy servers answer new HTTP connections beyond `http_preview_server.max_connections` with a retryable 503
instead of slowing everyone down.

## Safer vault key rotation

**Vault key rotation can no longer strand files on a dropped key.** A rotation now finishes only when every encrypted file was re-encrypted and committed; if any file fails, is open, or changes mid-way, the previous key stays loaded (so everything stays readable) and the next sync retries. Each file's new ciphertext is written to a durable side file and swapped in only after the database records its new IV, and an interrupted rotation is repaired on the next sync or restart by keeping whichever copy authenticates. Cloud vaults in Cache mode now rewrite their local copies too, remote-only files are re-encrypted without writing anything locally, and empty files no longer abort a rotation. File overwrites through the web and API also replace ciphertext atomically.

## Cloud vaults: local copies first, remote-only files readable

**Reading a cloud file that is stored locally no longer downloads it from S3.** Share-link downloads and previews and the built-in S3 gateway read the local encrypted copy, which costs no requests or egress (gateway range requests read only the requested bytes). **Files that exist only in the bucket (Cache mode) can now be previewed and downloaded:** the first read fetches the object once, after the same price-budget check sync uses and under a strict request cap, verifies it in full, and keeps it as a local encrypted copy (an object stored unencrypted upstream is encrypted on the way in; nothing decrypted touches the disk). Budget refusals are reported as unavailable instead of spending. Operators who would rather not keep copies can opt into metered range reads (`preview.media.remote: ranged`; note that individual ranges are not integrity-checked) or turn remote reads off (`off`). Fetched copies are not evicted yet.

## STEP/STP models in the 3D viewer (optional package)

Install `vaulthalla-preview-cad` to preview STEP and STP CAD models: the server converts each model to glTF once, and the web console shows it in the 3D viewer. Conversion runs in a separate, sandboxed helper process, never inside the daemon: it gets the file's bytes over a private channel (nothing decrypted is written to disk), cannot open files for writing, reach the network, start programs or interfere with the daemon, and is killed if it exceeds its memory, CPU-time, thread, wall-clock or output limits (`preview.derive.*` in `config.yaml`). A malformed or hostile model fails that one conversion and nothing else. The sandbox needs a kernel with Landlock enabled: without it the helpers refuse to run and conversions report "converter unavailable" (the daemon log says why). Helpers run only from root-owned locations, and `preview.derive.helper_dir` can be changed only in `config.yaml`, not from the web console.

`vaulthalla-preview-cad` and `vaulthalla-preview-media` are separate packages that `vaulthalla` only suggests, so the core package still installs without Open CASCADE or FFmpeg's libraries. Every new `preview.*` setting is optional; the shipped `config.yaml` lists them, commented out, with their defaults.

### Optional media helper (`vaulthalla-preview-media`)

- A new optional helper probes video and audio files (container, codecs, duration, and whether Chrome, Firefox
  and Safari can play them directly), renders poster frames, and transcodes to browser-safe H.264/AAC as
  fragmented MP4 or HLS. It runs out of process, reads plaintext only through the daemon's range channel (no
  plaintext temp files, and MP4 files with the index at the end are read in place), and sandboxes itself before
  touching input. Output size, duration, dimensions and wall time are capped.
- Hardware encoding (`preview.media.hwaccel`: VAAPI, Quick Sync, NVENC) is probed and falls back to software on
  any failure. Hardware paths have not yet been validated on real GPUs; software (libx264) is the tested path.

### Converted previews are cached encrypted

- Converted models, posters and transcodes are produced once per file version by a small background queue
  (`preview.derive.max_concurrency` at a time, at most `preview.derive.max_queue` waiting) and stored encrypted
  with the vault key. Editing a file makes the old conversion invalid automatically; deleting a file (moving it
  to the trash included) or removing a vault deletes its conversions; a finished key rotation drops conversions
  sealed under the old key. A file that cannot be converted is not retried until it changes (or for
  `preview.derive.failure_ttl_hours`).
- The cache stays within `caching.max_size_mb`, and conversions unused for `caching.thumbnails.expiry_days` are
  removed. On the first start after upgrading, thumbnails that older versions stored unencrypted are deleted;
  they are regenerated, encrypted, when next viewed.

## Web console
### Previews in the web console

The preview sheet now opens far more than images and PDFs, in the console and on share links:

- **Video and audio** play in the browser with seeking, straight from the encrypted vault (no autoplay). When
  your browser can't decode a format you get a Download button and, when the optional media converter is
  installed on the server, a "Convert for playback" option.
- **PDFs** page through every page (buttons or PageUp/PageDown), fitted to the page or the width.
- **3D models** (GLB, glTF, STL, OBJ, and STEP when the optional CAD converter is installed) open in an interactive viewer you can
  orbit, pan and zoom. The viewer only downloads when you open a model.
- **Text, code and Markdown** files open as text (Markdown rendered safely: no embedded HTML, no remote images).
  In the console you can edit and save them in place; if someone else saved the file since you opened it, you
  choose whether to reload their version, overwrite it, or copy your text, and nothing is lost silently.
- **SVG and WebP** images show as they are.
- Share links respect their permissions: a preview-only link shows image and PDF previews, and explains that
  video, audio, 3D and text need a link that allows downloads.
- Downloads of any size stream directly; the console checks a download with a lightweight request first instead
  of starting it twice.

### 3D models in the browser

GLB, glTF, STL and OBJ files open in an interactive 3D viewer: drag to orbit, right-drag or Ctrl-drag to pan,
scroll or pinch to zoom, with Fit, Reset, a model-sized ground grid, a wireframe toggle and a readout of meshes,
triangles, vertices, materials and bounding-box size. glTF files that keep their buffers and textures in separate
files, and OBJ files with a `.mtl` material library and textures, load those files from the same vault folder
(a missing texture shows the model untextured instead of failing). Draco- and meshopt-compressed glTF work offline:
their decoders ship with the console, and the viewer never contacts a third-party host (a model that points at
another host is not followed). Models over 20 million triangles, and glTF files that require KTX2/Basis compressed
textures, show an explanation instead of freezing the tab. The viewer (Babylon.js) loads only when a model is
opened, so the file browser and share pages load no extra JavaScript for it.
