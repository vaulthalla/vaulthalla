<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Rich file previews

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
