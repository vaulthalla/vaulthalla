<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Rich previews and safer key rotation

## Safer vault key rotation

**Vault key rotation can no longer strand files on a dropped key.** A rotation now finishes only when every encrypted file was re-encrypted and committed; if any file fails, is open, or changes mid-way, the previous key stays loaded (so everything stays readable) and the next sync retries. Each file's new ciphertext is written to a durable side file and swapped in only after the database records its new IV, and an interrupted rotation is repaired on the next sync or restart by keeping whichever copy authenticates. Cloud vaults in Cache mode now rewrite their local copies too, remote-only files are re-encrypted without writing anything locally, and empty files no longer abort a rotation. File overwrites through the web and API also replace ciphertext atomically.

## STEP/STP models in the 3D viewer (optional package)

Install `vaulthalla-preview-cad` to preview STEP and STP CAD models: the server converts each model to glTF once, and the web console shows it in the 3D viewer. Conversion runs in a separate, sandboxed helper process, never inside the daemon: it gets the file's bytes over a private channel (nothing decrypted is written to disk), cannot open files for writing, reach the network or start programs, and is killed if it exceeds its memory, CPU-time, wall-clock or output limits (`preview.derive.*` in `config.yaml`). A malformed or hostile model fails that one conversion and nothing else.

`vaulthalla-preview-cad` and `vaulthalla-preview-media` are separate packages that `vaulthalla` only suggests, so the core package still installs without Open CASCADE or FFmpeg's libraries. Every new `preview.*` setting is optional; the shipped `config.yaml` lists them, commented out, with their defaults.
