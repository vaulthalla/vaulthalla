// Cheap pre-scan of model bytes before Babylon touches them: refuse models that would freeze the tab, find the glTF
// extensions that need a decoder, and locate OBJ material references. No Babylon imports here, nothing allocates in
// proportion to the model beyond one pass over its bytes.

export type ModelFormat = 'glb' | 'gltf' | 'stl' | 'obj'

export const MODEL_LIMITS = {
  /** Triangles drawn per frame (instances included). Past this the GPU upload alone can take the tab down. */
  maxTriangles: 20_000_000,
  /** Unique vertices (shared geometry counted once). */
  maxVertices: 30_000_000,
  /** Text formats (OBJ, ASCII STL) are parsed on the main thread: refuse before parsing past this. */
  maxTextBytes: 512 * 1024 * 1024,
  /** The glTF JSON document (the binary buffers are separate). */
  maxGltfJsonBytes: 64 * 1024 * 1024,
  /** Textures larger than this are downscaled on upload. */
  maxTextureSize: 4096,
} as const

/** The model is over a safety limit; the message is user-facing. */
export class ModelLimitError extends Error {
  override name = 'ModelLimitError'
}

/** The model uses something the offline viewer can't decode (or isn't a model at all); the message is user-facing. */
export class ModelUnsupportedError extends Error {
  override name = 'ModelUnsupportedError'
}

export interface GltfInfo {
  extensionsUsed: string[]
  extensionsRequired: string[]
  /** `images[].uri` as written (texture files and data: URIs); a missing texture degrades instead of failing. */
  imageUris: string[]
}

export interface ObjMtlRef {
  /** Byte range of every `mtllib` line (without the line break). Babylon only loads the last one. */
  lines: { start: number; end: number }[]
  /** The material library the loader would request (last `mtllib` line, trimmed). */
  name: string
}

export interface ModelScan {
  triangles: number
  vertices: number
  gltf?: GltfInfo
  objMtl?: ObjMtlRef
}

const GLB_MAGIC = 0x46546c67 // "glTF"
const GLB_JSON_CHUNK = 0x4e4f534a // "JSON"

const formatCount = (n: number) =>
  n >= 1e6 ? `${(n / 1e6).toFixed(1)}M`
  : n >= 1e3 ? `${(n / 1e3).toFixed(1)}K`
  : String(Math.round(n))

const enforce = (scan: ModelScan): ModelScan => {
  if (scan.triangles > MODEL_LIMITS.maxTriangles)
    throw new ModelLimitError(
      `This model has about ${formatCount(scan.triangles)} triangles, more than the ${formatCount(MODEL_LIMITS.maxTriangles)} the browser viewer allows. Download it to open it in a desktop app.`,
    )
  if (scan.vertices > MODEL_LIMITS.maxVertices)
    throw new ModelLimitError(
      `This model has about ${formatCount(scan.vertices)} vertices, more than the ${formatCount(MODEL_LIMITS.maxVertices)} the browser viewer allows. Download it to open it in a desktop app.`,
    )
  return scan
}

const tooLargeText = (bytes: number) =>
  new ModelLimitError(
    `This model is ${(bytes / 1024 / 1024).toFixed(0)} MB of text, more than the ${MODEL_LIMITS.maxTextBytes / 1024 / 1024} MB the browser viewer parses. Download it to open it in a desktop app.`,
  )

// ---------------------------------------------------------------------------------------------------------- glTF

interface GltfJson {
  asset?: { version?: string }
  accessors?: { count?: number }[]
  meshes?: { primitives?: { attributes?: Record<string, number>; indices?: number; mode?: number }[] }[]
  nodes?: { mesh?: number; extensions?: { EXT_mesh_gpu_instancing?: { attributes?: Record<string, number> } } }[]
  images?: { uri?: unknown }[]
  extensionsUsed?: string[]
  extensionsRequired?: string[]
}

const isGlb = (data: ArrayBuffer) => data.byteLength >= 12 && new DataView(data).getUint32(0, true) === GLB_MAGIC

const glbJson = (data: ArrayBuffer): GltfJson => {
  const view = new DataView(data)
  const version = view.getUint32(4, true)
  if (version !== 2) throw new ModelUnsupportedError(`This is a glTF ${version}.0 binary; the viewer reads glTF 2.0.`)
  if (data.byteLength < 20) throw new ModelUnsupportedError('This GLB file is truncated.')
  const chunkLength = view.getUint32(12, true)
  const chunkType = view.getUint32(16, true)
  if (chunkType !== GLB_JSON_CHUNK || 20 + chunkLength > data.byteLength)
    throw new ModelUnsupportedError('This GLB file is damaged (its JSON chunk is missing or truncated).')
  if (chunkLength > MODEL_LIMITS.maxGltfJsonBytes)
    throw new ModelLimitError('This GLB file has an unusually large scene description; the viewer refuses it.')
  return parseJson(new Uint8Array(data, 20, chunkLength))
}

const parseJson = (bytes: Uint8Array): GltfJson => {
  try {
    const json = JSON.parse(new TextDecoder().decode(bytes)) as unknown
    if (!json || typeof json !== 'object') throw new Error('not an object')
    return json as GltfJson
  } catch {
    throw new ModelUnsupportedError('This glTF file is not valid JSON.')
  }
}

const countOf = (json: GltfJson, accessor: number | undefined) =>
  accessor === undefined ? undefined : Math.max(0, Number(json.accessors?.[accessor]?.count) || 0)

const scanGltfJson = (json: GltfJson): ModelScan => {
  const version = json.asset?.version
  if (version && !version.startsWith('2'))
    throw new ModelUnsupportedError(`This is a glTF ${version} file; the viewer reads glTF 2.0.`)

  // How many times each mesh is drawn: once per node that references it, times its GPU instances.
  const draws = new Map<number, number>()
  for (const node of json.nodes ?? []) {
    if (typeof node.mesh !== 'number') continue
    const instancing = node.extensions?.EXT_mesh_gpu_instancing?.attributes
    const instances = instancing ? Math.max(1, ...Object.values(instancing).map(a => countOf(json, a) ?? 1)) : 1
    draws.set(node.mesh, (draws.get(node.mesh) ?? 0) + instances)
  }

  let triangles = 0
  let vertices = 0
  ;(json.meshes ?? []).forEach((mesh, index) => {
    const times = Math.max(1, draws.get(index) ?? 0)
    for (const primitive of mesh.primitives ?? []) {
      const positions = countOf(json, primitive.attributes?.POSITION) ?? 0
      const elements = countOf(json, primitive.indices) ?? positions
      const mode = primitive.mode ?? 4
      const tris =
        mode === 4 ? elements / 3
        : mode === 5 || mode === 6 ? Math.max(0, elements - 2)
        : 0
      triangles += tris * times
      vertices += positions
    }
  })

  return {
    triangles,
    vertices,
    gltf: {
      extensionsUsed: Array.isArray(json.extensionsUsed) ? json.extensionsUsed.map(String) : [],
      extensionsRequired: Array.isArray(json.extensionsRequired) ? json.extensionsRequired.map(String) : [],
      imageUris: (json.images ?? []).map(image => image.uri).filter((uri): uri is string => typeof uri === 'string'),
    },
  }
}

// ----------------------------------------------------------------------------------------------------------- STL

const SPACE = 0x20
const TAB = 0x09
const CR = 0x0d
const LF = 0x0a
const isBlank = (b: number) => b === SPACE || b === TAB || b === CR

// Counts occurrences of an ASCII needle (lowercase) in the bytes, case-insensitively for letters.
const countToken = (bytes: Uint8Array, needle: string) => {
  const codes = Array.from(needle, c => c.charCodeAt(0))
  const first = codes[0]
  let count = 0
  outer: for (let i = 0, end = bytes.length - codes.length; i <= end; i++) {
    if ((bytes[i] | 0x20) !== first) continue
    for (let j = 1; j < codes.length; j++) if ((bytes[i + j] | 0x20) !== codes[j]) continue outer
    count++
    i += codes.length - 1
  }
  return count
}

const scanStl = (data: ArrayBuffer): ModelScan => {
  if (data.byteLength >= 84) {
    const declared = new DataView(data).getUint32(80, true)
    // Binary STL: 80-byte header, u32 count, 50 bytes per triangle. Some exporters pad the end, so allow extra bytes.
    if (84 + declared * 50 <= data.byteLength && 84 + declared * 50 > data.byteLength - 50)
      return enforce({ triangles: declared, vertices: declared * 3 })
  }
  const bytes = new Uint8Array(data)
  let i = 0
  while (i < bytes.length && (isBlank(bytes[i]) || bytes[i] === LF)) i++
  const ascii = new TextDecoder().decode(bytes.subarray(i, i + 5)).toLowerCase() === 'solid'
  if (!ascii) {
    // Not a consistent binary file and not ASCII: let the loader decide, but cap on what the size implies.
    const triangles = Math.max(0, Math.floor((data.byteLength - 84) / 50))
    return enforce({ triangles, vertices: triangles * 3 })
  }
  if (data.byteLength > MODEL_LIMITS.maxTextBytes) throw tooLargeText(data.byteLength)
  const triangles = countToken(bytes, 'endfacet')
  return enforce({ triangles, vertices: triangles * 3 })
}

// ----------------------------------------------------------------------------------------------------------- OBJ

const scanObj = (data: ArrayBuffer): ModelScan => {
  if (data.byteLength > MODEL_LIMITS.maxTextBytes) throw tooLargeText(data.byteLength)
  const bytes = new Uint8Array(data)
  const n = bytes.length
  let triangles = 0
  let vertices = 0
  const mtl: { start: number; end: number }[] = []

  let i = 0
  while (i < n) {
    // At a line start: skip indentation.
    while (i < n && (bytes[i] === SPACE || bytes[i] === TAB)) i++
    const start = i
    const c0 = bytes[i]
    const c1 = i + 1 < n ? bytes[i + 1] : LF
    if (c0 === 0x76 /* v */ && (c1 === SPACE || c1 === TAB)) {
      vertices++
    } else if (c0 === 0x66 /* f */ && (c1 === SPACE || c1 === TAB)) {
      // Polygon with k corners → k - 2 triangles once the loader fans it.
      let corners = 0
      let inToken = false
      for (i += 1; i < n && bytes[i] !== LF; i++) {
        const blank = isBlank(bytes[i])
        if (!blank && !inToken) corners++
        inToken = !blank
      }
      triangles += Math.max(0, corners - 2)
      i++
      continue
    } else if (
      c0 === 0x6d /* m */
      && bytes[i + 1] === 0x74
      && bytes[i + 2] === 0x6c
      && bytes[i + 3] === 0x6c
      && bytes[i + 4] === 0x69
      && bytes[i + 5] === 0x62
      && bytes[i + 6] === SPACE
    ) {
      let end = i
      while (end < n && bytes[end] !== LF) end++
      mtl.push({ start, end: end > start && bytes[end - 1] === CR ? end - 1 : end })
      i = end + 1
      continue
    }
    while (i < n && bytes[i] !== LF) i++
    i++
  }

  const scan: ModelScan = { triangles, vertices }
  if (mtl.length) {
    const last = mtl[mtl.length - 1]
    const name = new TextDecoder().decode(bytes.subarray(last.start + 7, last.end)).trim()
    if (name) scan.objMtl = { lines: mtl, name }
  }
  return enforce(scan)
}

/**
 * Rewrites the OBJ's `mtllib` lines: the last one points at `replacement` (an object URL of the rewritten material
 * library), the others become comments. With `replacement === null` every `mtllib` line becomes a comment, so the
 * loader never requests a sibling file on its own (it would resolve against the page URL). Returns a new buffer.
 */
export const rewriteObjMtl = (
  data: ArrayBuffer,
  ref: ObjMtlRef,
  replacement: string | null,
): Uint8Array<ArrayBuffer> => {
  const source = new Uint8Array(data)
  const last = ref.lines[ref.lines.length - 1]
  const line = replacement === null ? null : new TextEncoder().encode(`mtllib ${replacement}`)
  const delta = line ? line.length - (last.end - last.start) : 0
  const out = new Uint8Array(source.length + delta)
  out.set(source.subarray(0, last.start), 0)
  if (line) out.set(line, last.start)
  else out.set(source.subarray(last.start, last.end), last.start)
  out.set(source.subarray(last.end), last.end + delta)
  for (const { start } of replacement === null ? ref.lines : ref.lines.slice(0, -1)) out[start] = 0x23 // '#'
  return out
}

/** Texture statements Babylon's MTL loader reads; the file name is the last token (options come first). */
const MTL_TEXTURE_KEYS = new Set(['map_ka', 'map_kd', 'map_ks', 'map_bump', 'bump', 'map_d'])

/**
 * Rewrites a material library so every texture it names is an object URL produced by `resolve` (or dropped when it
 * can't be resolved). Returns the new MTL text.
 */
export const rewriteMtlTextures = async (mtl: string, resolve: (name: string) => Promise<string | null>) => {
  const lines = mtl.split(/\r?\n/)
  const out: string[] = []
  for (const raw of lines) {
    const line = raw.trim()
    const tokens = line.split(/\s+/)
    const key = tokens[0]?.toLowerCase()
    if (!key || !MTL_TEXTURE_KEYS.has(key) || tokens.length < 2) {
      out.push(raw)
      continue
    }
    const name = tokens[tokens.length - 1]
    const url = await resolve(name)
    if (!url) continue
    // Keep a bump multiplier (-bm x): Babylon reads it from the same line.
    const bm = tokens.indexOf('-bm')
    out.push(bm > 0 && bm + 1 < tokens.length - 1 ? `${tokens[0]} -bm ${tokens[bm + 1]} ${url}` : `${tokens[0]} ${url}`)
  }
  return out.join('\n')
}

// ---------------------------------------------------------------------------------------------------------- entry

/** Throws ModelLimitError / ModelUnsupportedError with a user-facing message. */
export const scanModel = (data: ArrayBuffer, format: ModelFormat): ModelScan => {
  if (data.byteLength === 0) throw new ModelUnsupportedError('This file is empty.')
  switch (format) {
    case 'glb':
    case 'gltf': {
      // Trust the bytes over the extension: a .gltf that is really a GLB (or the reverse) still loads.
      if (isGlb(data)) return enforce(scanGltfJson(glbJson(data)))
      if (data.byteLength > MODEL_LIMITS.maxGltfJsonBytes)
        throw new ModelLimitError('This glTF file is too large to parse in the browser. Download it instead.')
      return enforce(scanGltfJson(parseJson(new Uint8Array(data))))
    }
    case 'stl':
      return scanStl(data)
    case 'obj':
      return scanObj(data)
  }
}

/** The glTF extensions whose decoders aren't shipped with the console, and what to tell the user. */
export const UNSUPPORTED_REQUIRED_GLTF_EXTENSIONS: Record<string, string> = {
  KHR_texture_basisu:
    'This glTF requires KTX2/Basis compressed textures, which the self-hosted viewer does not decode. Download it to open it in a desktop app.',
}
