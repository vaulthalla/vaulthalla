<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Rich file previews

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
