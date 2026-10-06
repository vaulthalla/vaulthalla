#!/usr/bin/env node
// Regenerates the preview e2e fixtures in this folder (each well under 300 KB). Media needs the ffmpeg CLI
// (libvpx-vp9, libopus, mpeg4); the PDF and 3D models are written by hand here.
//   node web/tests/e2e/fixtures/preview/generate.mjs
import { execFileSync } from 'node:child_process'
import { writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const dir = dirname(fileURLToPath(import.meta.url))
const out = name => join(dir, name)
const ffmpeg = (...args) => execFileSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', ...args], { stdio: 'inherit' })

// Image: one 320x240 test card.
ffmpeg('-f', 'lavfi', '-i', 'testsrc=size=320x240:rate=1', '-frames:v', '1', out('image.png'))

// Video every Chromium build plays (VP9 + Opus WebM), 8 s, a keyframe every second so seeks land quickly.
ffmpeg(
  '-f', 'lavfi', '-i', 'testsrc=size=192x108:rate=25', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
  '-t', '8', '-c:v', 'libvpx-vp9', '-b:v', '120k', '-g', '25', '-c:a', 'libopus', '-b:a', '24k', out('clip.webm'),
)

// Audio: 3 s Opus in Ogg.
ffmpeg('-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=48000', '-t', '3', '-c:a', 'libopus', '-b:a', '24k', out('tone.ogg'))

// A container no browser plays (MPEG-4 Part 2 in AVI): the "can't play in your browser" fallback.
ffmpeg('-f', 'lavfi', '-i', 'testsrc=size=160x90:rate=10', '-t', '2', '-c:v', 'mpeg4', '-q:v', '10', out('unsupported.avi'))

// Two-page PDF with a correct xref table.
const pdf = () => {
  const page = contentId =>
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Resources << /Font << /F1 5 0 R >> >> /Contents ${contentId} 0 R >>`
  const stream = text => {
    const body = `BT /F1 20 Tf 30 90 Td (${text}) Tj ET`
    return `<< /Length ${body.length} >>\nstream\n${body}\nendstream`
  }
  const objects = [
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>',
    page(6),
    page(7),
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    stream('Vaulthalla page one'),
    stream('Vaulthalla page two'),
  ]
  let doc = '%PDF-1.4\n'
  const offsets = []
  objects.forEach((body, i) => {
    offsets.push(doc.length)
    doc += `${i + 1} 0 obj\n${body}\nendobj\n`
  })
  const xref = doc.length
  doc += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`
  for (const offset of offsets) doc += `${String(offset).padStart(10, '0')} 00000 n \n`
  doc += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`
  return doc
}
writeFileSync(out('doc.pdf'), pdf(), 'latin1')

writeFileSync(out('notes.txt'), 'Vaulthalla preview notes\nline two\n')
writeFileSync(
  out('readme.md'),
  '# Preview readme\n\nSome **bold** text and a [link](https://example.com).\n\n<img src="x" onerror="window.__pwned = true">\n\n![remote](https://example.com/x.png)\n\n| a | b |\n|---|---|\n| 1 | 2 |\n',
)

// A unit cube with per-face normals (24 vertices, 12 triangles).
const FACES = [
  { n: [1, 0, 0], c: [[1, -1, -1], [1, 1, -1], [1, 1, 1], [1, -1, 1]] },
  { n: [-1, 0, 0], c: [[-1, -1, 1], [-1, 1, 1], [-1, 1, -1], [-1, -1, -1]] },
  { n: [0, 1, 0], c: [[-1, 1, -1], [-1, 1, 1], [1, 1, 1], [1, 1, -1]] },
  { n: [0, -1, 0], c: [[-1, -1, 1], [-1, -1, -1], [1, -1, -1], [1, -1, 1]] },
  { n: [0, 0, 1], c: [[-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]] },
  { n: [0, 0, -1], c: [[1, -1, -1], [-1, -1, -1], [-1, 1, -1], [1, 1, -1]] },
]
const positions = FACES.flatMap(f => f.c.flat())
const normals = FACES.flatMap(f => [f.n, f.n, f.n, f.n].flat())
const indices = FACES.flatMap((_, i) => [0, 1, 2, 0, 2, 3].map(k => i * 4 + k))

const gltfParts = () => {
  const pos = Buffer.from(new Float32Array(positions).buffer)
  const nrm = Buffer.from(new Float32Array(normals).buffer)
  const idx = Buffer.from(new Uint16Array(indices).buffer)
  const bin = Buffer.concat([pos, nrm, idx, Buffer.alloc((4 - (idx.length % 4)) % 4)])
  const json = {
    asset: { version: '2.0', generator: 'vaulthalla e2e fixtures' },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ mesh: 0 }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0, NORMAL: 1 }, indices: 2, material: 0 }] }],
    materials: [{ pbrMetallicRoughness: { baseColorFactor: [0.13, 0.83, 0.93, 1], metallicFactor: 0, roughnessFactor: 0.6 } }],
    buffers: [{ byteLength: bin.length }],
    bufferViews: [
      { buffer: 0, byteOffset: 0, byteLength: pos.length, target: 34962 },
      { buffer: 0, byteOffset: pos.length, byteLength: nrm.length, target: 34962 },
      { buffer: 0, byteOffset: pos.length + nrm.length, byteLength: idx.length, target: 34963 },
    ],
    accessors: [
      { bufferView: 0, componentType: 5126, count: 24, type: 'VEC3', min: [-1, -1, -1], max: [1, 1, 1] },
      { bufferView: 1, componentType: 5126, count: 24, type: 'VEC3' },
      { bufferView: 2, componentType: 5123, count: 36, type: 'SCALAR' },
    ],
  }
  return { json, bin }
}

{
  const { json, bin } = gltfParts()
  const text = Buffer.from(JSON.stringify(json))
  const jsonChunk = Buffer.concat([text, Buffer.alloc((4 - (text.length % 4)) % 4, 0x20)])
  const chunk = (type, data) => {
    const head = Buffer.alloc(8)
    head.writeUInt32LE(data.length, 0)
    head.writeUInt32LE(type, 4)
    return Buffer.concat([head, data])
  }
  const header = Buffer.alloc(12)
  header.writeUInt32LE(0x46546c67, 0) // "glTF"
  header.writeUInt32LE(2, 4)
  header.writeUInt32LE(12 + 8 + jsonChunk.length + 8 + bin.length, 8)
  writeFileSync(out('cube.glb'), Buffer.concat([header, chunk(0x4e4f534a, jsonChunk), chunk(0x004e4942, bin)]))
}

// glTF with an external buffer: exercises sibling resolution (cube.bin next to cube.gltf).
{
  const { json, bin } = gltfParts()
  json.buffers[0].uri = 'cube.bin'
  writeFileSync(out('cube.gltf'), JSON.stringify(json, null, 1))
  writeFileSync(out('cube.bin'), bin)
}

// ASCII STL and OBJ of the same cube.
{
  let stl = 'solid cube\n'
  for (let t = 0; t < indices.length; t += 3) {
    const face = FACES[Math.floor(indices[t] / 4)]
    stl += `  facet normal ${face.n.join(' ')}\n    outer loop\n`
    for (let k = 0; k < 3; k++) stl += `      vertex ${positions.slice(indices[t + k] * 3, indices[t + k] * 3 + 3).join(' ')}\n`
    stl += '    endloop\n  endfacet\n'
  }
  writeFileSync(out('cube.stl'), `${stl}endsolid cube\n`)

  let obj = '# unit cube\no cube\n'
  for (let v = 0; v < positions.length; v += 3) obj += `v ${positions.slice(v, v + 3).join(' ')}\n`
  for (let v = 0; v < normals.length; v += 3) obj += `vn ${normals.slice(v, v + 3).join(' ')}\n`
  for (let t = 0; t < indices.length; t += 3) obj += `f ${[0, 1, 2].map(k => `${indices[t + k] + 1}//${indices[t + k] + 1}`).join(' ')}\n`
  writeFileSync(out('cube.obj'), obj)
}
