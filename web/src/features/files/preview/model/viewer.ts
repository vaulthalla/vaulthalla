// The Babylon side of the model viewer: one engine + scene per mounted viewer, loading from memory, framing the
// camera to the model, a model-sized grid and the stats readout. Babylon is imported here and in ModelViewer.tsx (and
// the decoders module) only; every import below is a named import of a granular module (never the barrel), so the
// chunk carries only what the viewer uses. Format loaders are imported dynamically per format.

import { Engine } from '@babylonjs/core/Engines/engine'
import { Scene } from '@babylonjs/core/scene'
import { ArcRotateCamera } from '@babylonjs/core/Cameras/arcRotateCamera'
import { HemisphericLight } from '@babylonjs/core/Lights/hemisphericLight'
import { DirectionalLight } from '@babylonjs/core/Lights/directionalLight'
import { Vector3 } from '@babylonjs/core/Maths/math.vector'
import { Color3, Color4 } from '@babylonjs/core/Maths/math.color'
import { CreateLineSystem } from '@babylonjs/core/Meshes/Builders/linesBuilder'
import { LoadAssetContainerAsync } from '@babylonjs/core/Loading/sceneLoader'
import { SceneLoaderFlags } from '@babylonjs/core/Loading/sceneLoaderFlags'
import { Tools } from '@babylonjs/core/Misc/tools'
import type { AbstractMesh } from '@babylonjs/core/Meshes/abstractMesh'
import type { LinesMesh } from '@babylonjs/core/Meshes/linesMesh'
import type { Mesh } from '@babylonjs/core/Meshes/mesh'
import type { AssetContainer } from '@babylonjs/core/assetContainer'
import type { ModelStats } from '../ModelViewer'
import {
  MODEL_LIMITS,
  ModelLimitError,
  ModelUnsupportedError,
  UNSUPPORTED_REQUIRED_GLTF_EXTENSIONS,
  decodeModelText,
  rewriteMtlTextures,
  sanitizeObjText,
  scanModel,
  withObjMtllib,
  type ModelFormat,
  type ModelScan,
} from './scan'
import { allowUrl, installUrlGate, isAllowedUrl } from './urlGate'

/** No WebGL at all (or the context could not be created); the message is user-facing. */
export class ModelWebGLError extends Error {
  override name = 'ModelWebGLError'
}

// Never show Babylon's own loading UI (the sheet draws its own), and never fetch from Babylon's CDN: every decoder
// the viewer needs is bundled (see decoders.ts). Any code path that still asks Babylon for a CDN script
// (cdn.babylonjs.com: KTX2/Basis transcoders, glTF validator) or a CDN asset (assets.babylonjs.com: area-light LUT,
// blue-noise texture) is rewritten to this same-origin path, which 404s instead of reaching a third-party host.
// The URL gate (urlGate.ts) refuses those rewritten URLs as well, so they fail locally without a request.
const NO_CDN = '/_next/static/vh-babylon-cdn-disabled'
SceneLoaderFlags.ShowLoadingScreen = false
Tools.ScriptBaseUrl = NO_CDN
Tools.AssetBaseUrl = NO_CDN
installUrlGate()

const MAX_DPR = 2
const GLB_MAGIC = 0x46546c67 // "glTF"
// 1×1 white PNG standing in for a texture that can't be resolved.
const PLACEHOLDER_TEXTURE =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4//8/AAX+Av4N70a4AAAAAElFTkSuQmCC'
const DEFAULT_ALPHA = -Math.PI / 2 + 0.6
const DEFAULT_BETA = 1.1

/** Reads a theme token (`#rrggbb` or `rgb(r g b / a)`) into a Babylon colour. */
const cssColor = (name: string, fallback: [number, number, number, number]): Color4 => {
  const raw =
    typeof window === 'undefined' ? '' : getComputedStyle(document.documentElement).getPropertyValue(name).trim()
  const hex = /^#([0-9a-f]{6})$/i.exec(raw)
  if (hex) {
    const v = parseInt(hex[1], 16)
    return new Color4(((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255, 1)
  }
  const rgb = /^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)(?:\s*[/,]\s*([\d.]+%?))?\s*\)$/i.exec(raw)
  if (rgb) {
    const alpha =
      rgb[4] === undefined ? 1
      : rgb[4].endsWith('%') ? parseFloat(rgb[4]) / 100
      : parseFloat(rgb[4])
    return new Color4(+rgb[1] / 255, +rgb[2] / 255, +rgb[3] / 255, alpha)
  }
  return new Color4(...fallback)
}

const MIME_BY_EXTENSION: Record<string, string> = {
  png: 'image/png',
  jpg: 'image/jpeg',
  jpeg: 'image/jpeg',
  webp: 'image/webp',
  avif: 'image/avif',
  gif: 'image/gif',
  bmp: 'image/bmp',
  bin: 'application/octet-stream',
}

const mimeFor = (name: string) =>
  MIME_BY_EXTENSION[name.split('.').pop()?.toLowerCase() ?? ''] ?? 'application/octet-stream'

/** glTF URIs are percent-encoded relative references; OBJ/MTL names are raw paths, sometimes with backslashes. */
const normalizeRef = (uri: string) => {
  let ref = uri.trim().replace(/\\/g, '/')
  try {
    ref = decodeURIComponent(ref)
  } catch {
    // keep the raw form
  }
  return ref.replace(/^\.\//, '')
}

/** Absolute and scheme-qualified references are never fetched: a model must not make the browser call other hosts. */
const isExternalRef = (uri: string) => /^[a-z][a-z0-9+.-]*:/i.test(uri) || uri.startsWith('//') || uri.startsWith('/')

export interface ResolveInit {
  /** Aborted when the viewer is disposed or the load fails; pass it to every request. */
  signal: AbortSignal
  /** Refuse a body larger than this (what is left of the model's total byte budget) by throwing a RangeError. */
  maxBytes: number
  /** Report bytes received so far for this file; the viewer aborts every request once the total is over budget. */
  onProgress: (loaded: number) => void
}

export interface ViewerOptions {
  resolveResource?: (uri: string, init: ResolveInit) => Promise<ArrayBuffer>
}

const formatMiB = (bytes: number) => `${Math.round(bytes / 1024 / 1024)} MB`

const tooManyResources = () =>
  new ModelLimitError(
    `This model references more than ${MODEL_LIMITS.maxResources} separate files, more than the browser viewer loads. Download it to open it in a desktop app.`,
  )

const tooManyBytes = () =>
  new ModelLimitError(
    `This model and the files it references add up to more than ${formatMiB(MODEL_LIMITS.maxTotalBytes)}, more than the browser viewer loads. Download it to open it in a desktop app.`,
  )

const abortError = () => new DOMException('The viewer was closed.', 'AbortError')

export interface Viewer {
  load: (data: ArrayBuffer, format: ModelFormat, fileName: string) => Promise<ModelStats>
  fit: () => void
  reset: () => void
  setGrid: (visible: boolean) => void
  setWireframe: (enabled: boolean) => void
  /** Called once if the WebGL context is lost (the viewer is dead afterwards; remount to retry). */
  onContextLost: (handler: () => void) => void
  dispose: () => void
}

export const createViewer = (canvas: HTMLCanvasElement, options: ViewerOptions): Viewer => {
  let engine: Engine
  try {
    engine = new Engine(
      canvas,
      true,
      {
        // Lets tests (and screenshots) read the canvas back after a frame; a modest cost for a single preview canvas.
        preserveDrawingBuffer: true,
        stencil: false,
        premultipliedAlpha: false,
        powerPreference: 'default',
        failIfMajorPerformanceCaveat: false,
        // Restoring after a context loss means keeping CPU copies of every buffer; the viewer offers a reload instead.
        doNotHandleContextLost: true,
        audioEngine: false,
      },
      false,
    )
  } catch (error) {
    throw new ModelWebGLError(
      `3D preview needs WebGL, which this browser or device does not provide${error instanceof Error && error.message ? ` (${error.message})` : ''}.`,
    )
  }
  if (!engine._gl) {
    engine.dispose()
    throw new ModelWebGLError('3D preview needs WebGL, which this browser or device does not provide.')
  }
  engine.setHardwareScalingLevel(1 / Math.min(window.devicePixelRatio || 1, MAX_DPR))
  // Textures above this are downscaled on upload instead of exhausting GPU memory.
  const caps = engine.getCaps()
  caps.maxTextureSize = Math.min(caps.maxTextureSize, MODEL_LIMITS.maxTextureSize)
  caps.maxCubemapTextureSize = Math.min(caps.maxCubemapTextureSize, MODEL_LIMITS.maxTextureSize)

  installUrlGate()
  const scene = new Scene(engine)
  scene.clearColor = cssColor('--surface-solid', [0.043, 0.067, 0.098, 1])
  scene.skipPointerMovePicking = true
  scene.skipFrustumClipping = false

  const camera = new ArcRotateCamera('vh-camera', DEFAULT_ALPHA, DEFAULT_BETA, 4, Vector3.Zero(), scene)
  camera.wheelDeltaPercentage = 0.01
  camera.pinchDeltaPercentage = 0.01
  camera.panningInertia = 0.85
  camera.attachControl(true)

  const hemi = new HemisphericLight('vh-hemi', new Vector3(0, 1, 0), scene)
  hemi.intensity = 0.7
  hemi.groundColor = new Color3(0.28, 0.3, 0.34)
  const key = new DirectionalLight('vh-key', new Vector3(-0.4, -1, 0.6), scene)
  key.intensity = 0.8
  // Untextured formats (STL, OBJ without a material library) get a neutral matte grey instead of Babylon's white.
  // StandardMaterial comes with those loaders' chunks, so glTF-only sessions never load it.
  const applyNeutralMaterial = async () => {
    const { StandardMaterial } = await import('@babylonjs/core/Materials/standardMaterial')
    const neutral = new StandardMaterial('vh-neutral', scene)
    neutral.diffuseColor = new Color3(0.7, 0.73, 0.77)
    neutral.specularColor = new Color3(0.12, 0.12, 0.12)
    scene.defaultMaterial = neutral
  }

  let container: AssetContainer | null = null
  let grid: LinesMesh | null = null
  let gridVisible = true
  let disposed = false
  // Aborts every side-file request still running when the viewer goes away.
  const lifetime = new AbortController()

  // Object URLs for resolved side files: the only blob: URLs the URL gate lets Babylon load. Revoked (and removed
  // from the gate) on dispose; never created once the viewer is gone, so nothing outlives it.
  const objectUrls: { url: string; release: () => void }[] = []
  const toObjectUrl = (bytes: BlobPart, name: string) => {
    if (disposed) throw abortError()
    const url = URL.createObjectURL(new Blob([bytes], { type: mimeFor(name) }))
    objectUrls.push({ url, release: allowUrl(url) })
    return url
  }
  // The model's bounding sphere, used for framing and the per-frame clip planes.
  let center = Vector3.Zero()
  let radius = 1
  let farReach = 1

  const modelMeshes = (): AbstractMesh[] =>
    (container?.meshes ?? []).filter(mesh => mesh.getTotalVertices() > 0 && mesh.isEnabled())

  const buildGrid = (min: Vector3, max: Vector3) => {
    grid?.dispose()
    const size = max.subtract(min)
    const extent = Math.max(size.x, size.z, size.y * 0.5, 1e-6) * 1.6
    // A "nice" spacing (1, 2 or 5 × 10^n) giving about 20 cells across.
    const raw = extent / 20
    const magnitude = 10 ** Math.floor(Math.log10(raw))
    const step = [1, 2, 5, 10].map(m => m * magnitude).find(s => s >= raw) ?? raw
    const cells = Math.ceil(extent / 2 / step)
    const half = cells * step
    const cx = Math.round((min.x + max.x) / 2 / step) * step
    const cz = Math.round((min.z + max.z) / 2 / step) * step
    const y = min.y
    const lines: Vector3[][] = []
    for (let i = -cells; i <= cells; i++) {
      lines.push([new Vector3(cx + i * step, y, cz - half), new Vector3(cx + i * step, y, cz + half)])
      lines.push([new Vector3(cx - half, y, cz + i * step), new Vector3(cx + half, y, cz + i * step)])
    }
    const mesh = CreateLineSystem('vh-grid', { lines }, scene)
    const tone = cssColor('--fg', [0.9, 0.93, 0.96, 1])
    mesh.color = new Color3(tone.r, tone.g, tone.b)
    mesh.alpha = 0.14
    mesh.isPickable = false
    mesh.setEnabled(gridVisible)
    grid = mesh
    farReach = Math.max(radius, half * Math.SQRT2)
  }

  const frame = (resetAngles: boolean) => {
    if (resetAngles) {
      camera.alpha = DEFAULT_ALPHA
      camera.beta = DEFAULT_BETA
    }
    camera.target = center.clone()
    // Fit the bounding sphere in the narrower of the two fields of view.
    const aspect = engine.getAspectRatio(camera) || 1
    const vertical = camera.fov
    const horizontal = 2 * Math.atan(Math.tan(vertical / 2) * aspect)
    const distance = (radius / Math.sin(Math.min(vertical, horizontal) / 2)) * 1.08
    camera.radius = distance
    camera.lowerRadiusLimit = radius * 0.02
    camera.upperRadiusLimit = distance * 20
    camera.inertialAlphaOffset = 0
    camera.inertialBetaOffset = 0
    camera.inertialRadiusOffset = 0
    camera.inertialPanningX = 0
    camera.inertialPanningY = 0
  }

  // Clip planes and pan speed follow the camera: near as far out as the geometry allows (no z-fighting on huge
  // models), far just past the model and grid (nothing clipped on tiny ones), and panning in screen-space units.
  // The view matrix is rebuilt before the projection in each frame, so updating here applies to the same frame.
  camera.onViewMatrixChangedObservable.add(() => {
    const distance = Vector3.Distance(camera.position, center)
    const near = Math.max(distance - radius, distance * 0.002, radius * 1e-4)
    camera.minZ = near * 0.9
    camera.maxZ = (distance + farReach) * 1.1
    camera.panningSensibility = 3000 / Math.max(camera.radius, 1e-9)
    // Key light rides with the camera (from above-left), so the visible side is never in shadow.
    const forward = camera.target.subtract(camera.position).normalize()
    key.direction.set(forward.x * 0.8 - 0.25, forward.y * 0.8 - 0.5, forward.z * 0.8)
  })

  const resize = () => {
    if (!disposed) engine.resize()
  }
  const observer = typeof ResizeObserver === 'undefined' ? null : new ResizeObserver(resize)
  observer?.observe(canvas)
  engine.runRenderLoop(() => {
    if (scene.activeCamera) scene.render()
  })

  // With doNotHandleContextLost Babylon only drops its GL state and raises nothing, so listen on the canvas. Not
  // calling preventDefault() means the browser won't restore this context: the UI remounts a fresh canvas instead.
  let contextLost: (() => void) | null = null
  const onContextLost = () => {
    engine.stopRenderLoop()
    contextLost?.()
  }
  canvas.addEventListener('webglcontextlost', onContextLost)

  // One load's side-file budget: at most MODEL_LIMITS.maxResources files and MODEL_LIMITS.maxTotalBytes in all
  // (the model included). Over budget, every request of the load is aborted and the load fails with `limit`.
  interface LoadBudget {
    signal: AbortSignal
    count: number
    bytes: number
    limit: ModelLimitError | null
    exceed: (error: ModelLimitError) => ModelLimitError
    cancel: () => void
  }

  const startBudget = (modelBytes: number): LoadBudget => {
    const controller = new AbortController()
    const stop = () => controller.abort(lifetime.signal.reason)
    lifetime.signal.addEventListener('abort', stop, { once: true })
    const budget: LoadBudget = {
      signal: controller.signal,
      count: 0,
      bytes: modelBytes,
      limit: null,
      exceed: error => {
        budget.limit ??= error
        controller.abort(budget.limit)
        return budget.limit
      },
      cancel: () => {
        lifetime.signal.removeEventListener('abort', stop)
        controller.abort(abortError())
      },
    }
    if (modelBytes > MODEL_LIMITS.maxTotalBytes) budget.exceed(tooManyBytes())
    return budget
  }

  const resolveRef = async (uri: string, budget: LoadBudget): Promise<ArrayBuffer> => {
    const ref = normalizeRef(uri)
    if (!ref || isExternalRef(ref))
      throw new Error(`it references a file on another host (${uri.slice(0, 120)}), which is never fetched`)
    if (!options.resolveResource) throw new Error(`it needs a separate file (${ref}) that is not available here`)
    if (budget.limit) throw budget.limit
    if (budget.signal.aborted) throw budget.signal.reason
    if (++budget.count > MODEL_LIMITS.maxResources) throw budget.exceed(tooManyResources())
    let counted = 0
    const count = (loaded: number) => {
      budget.bytes += loaded - counted
      counted = loaded
      if (budget.bytes > MODEL_LIMITS.maxTotalBytes) budget.exceed(tooManyBytes())
    }
    try {
      const bytes = await options.resolveResource(ref, {
        signal: budget.signal,
        maxBytes: Math.max(0, MODEL_LIMITS.maxTotalBytes - budget.bytes),
        onProgress: count,
      })
      count(bytes.byteLength)
      if (budget.limit) throw budget.limit
      return bytes
    } catch (error) {
      if (budget.limit) throw budget.limit
      if (error instanceof RangeError) throw budget.exceed(tooManyBytes())
      if (budget.signal.aborted) throw budget.signal.reason
      const reason = error instanceof Error && error.message ? `: ${error.message}` : ''
      throw new Error(`it needs a separate file (${ref}) that could not be read${reason}`)
    }
  }

  const gltfOptions = (scan: ModelScan, budget: LoadBudget) => {
    const used = new Set(scan.gltf?.extensionsUsed ?? [])
    const images = new Set(scan.gltf?.imageUris ?? [])
    return {
      // Draw the model as authored; inspection does not need animation playback to start.
      animationStartMode: 0,
      // Every URI the glTF loader would load passes here (base64 data: URIs are decoded before). data: URIs and the
      // viewer's own object URLs load as they are; everything else is a sibling file resolved through the caller,
      // never a URL of the model's choosing (a `blob:` or absolute URI is refused like any other external one). A
      // texture that can't be resolved becomes a white pixel so the geometry still shows; a missing buffer, the
      // budget running out or the viewer closing fails the load.
      preprocessUrlAsync: async (url: string) => {
        if (isAllowedUrl(url)) return url
        try {
          return toObjectUrl(await resolveRef(url, budget), url)
        } catch (error) {
          if (!images.has(url) || budget.limit || budget.signal.aborted) throw error
          console.warn(`[model] texture ${url.slice(0, 120)} skipped: ${error instanceof Error ? error.message : error}`)
          return PLACEHOLDER_TEXTURE
        }
      },
      extensionOptions: used.has('KHR_texture_basisu') ? { KHR_texture_basisu: { enabled: false } } : {},
    }
  }

  const prepareGltf = async (scan: ModelScan) => {
    for (const required of scan.gltf?.extensionsRequired ?? [])
      if (UNSUPPORTED_REQUIRED_GLTF_EXTENSIONS[required])
        throw new ModelUnsupportedError(UNSUPPORTED_REQUIRED_GLTF_EXTENSIONS[required])
    const used = new Set(scan.gltf?.extensionsUsed ?? [])
    const [{ registerBuiltInGLTFExtensions }] = await Promise.all([
      import('@babylonjs/loaders/glTF/2.0/Extensions/dynamic'),
      import('@babylonjs/loaders/glTF/2.0/glTFLoader'),
    ])
    registerBuiltInGLTFExtensions()
    if (used.has('KHR_draco_mesh_compression') || used.has('EXT_meshopt_compression')) {
      const decoders = await import('./decoders')
      if (used.has('KHR_draco_mesh_compression')) await decoders.ensureDraco()
      if (used.has('EXT_meshopt_compression')) await decoders.ensureMeshopt()
    }
  }

  // The OBJ as Babylon will load it. Every material-library statement is removed from the text (whatever its case or
  // whitespace; see sanitizeObjText). If the one Babylon would have used resolves to a sibling file, the material
  // library is rewritten so each texture is an object URL (or dropped), and one `mtllib <object URL>` line is
  // appended; otherwise the loader is told to skip materials altogether.
  const prepareObj = async (data: ArrayBuffer, budget: LoadBudget): Promise<{ source: BlobPart; skipMaterials: boolean }> => {
    await import('@babylonjs/loaders/OBJ/objFileLoader')
    const decoded = decodeModelText(new Uint8Array(data))
    const { text, mtllib } = sanitizeObjText(decoded)
    const source = text ?? data
    if (!mtllib || !options.resolveResource) return { source, skipMaterials: true }
    let mtlUrl: string | null = null
    try {
      const mtl = decodeModelText(new Uint8Array(await resolveRef(mtllib, budget)))
      const textures = new Map<string, Promise<string | null>>()
      const rewritten = await rewriteMtlTextures(mtl, name => {
        if (!textures.has(name))
          textures.set(
            name,
            resolveRef(name, budget).then(
              bytes => toObjectUrl(bytes, name),
              () => null,
            ),
          )
        return textures.get(name)!
      })
      if (budget.limit) throw budget.limit
      mtlUrl = toObjectUrl(rewritten, 'materials.mtl')
    } catch (error) {
      if (budget.limit || budget.signal.aborted) throw error
      mtlUrl = null // untextured, default material
    }
    return mtlUrl ? { source: withObjMtllib(text ?? decoded, mtlUrl), skipMaterials: false } : { source, skipMaterials: true }
  }


  const computeStats = (): ModelStats => {
    const meshes = modelMeshes()
    let min = new Vector3(Infinity, Infinity, Infinity)
    let max = new Vector3(-Infinity, -Infinity, -Infinity)
    let vertices = 0
    let triangles = 0
    for (const mesh of meshes) {
      mesh.computeWorldMatrix(true)
      mesh.refreshBoundingInfo({})
      const box = mesh.getBoundingInfo().boundingBox
      min = Vector3.Minimize(min, box.minimumWorld)
      max = Vector3.Maximize(max, box.maximumWorld)
      const indices = mesh.getTotalIndices()
      triangles += Math.floor((indices > 0 ? indices : mesh.getTotalVertices()) / 3)
      // Instances share their source's vertices: count geometry once.
      if (mesh.getClassName() !== 'InstancedMesh') vertices += mesh.getTotalVertices()
    }
    if (!meshes.length) min = max = Vector3.Zero()
    const size = max.subtract(min)
    const round = (v: Vector3): [number, number, number] => [v.x, v.y, v.z]
    return {
      meshes: meshes.length,
      vertices,
      triangles,
      materials: container?.materials.length ?? 0,
      bounds: { min: round(min), max: round(max), size: round(size) },
    }
  }

  const load = async (data: ArrayBuffer, format: ModelFormat, fileName: string): Promise<ModelStats> => {
    const scan = scanModel(data, format)
    const budget = startBudget(data.byteLength)
    // How the loader gets the bytes from memory: binary glTF as a buffer view; JSON glTF through the glTF plugin's
    // direct-load path (a `data:` string, the only way it accepts JSON from memory); STL and OBJ as a File, since
    // those plugins don't take buffer views. Trust the bytes over the name for glTF.
    let source: string | File | Uint8Array<ArrayBuffer>
    let pluginExtension = `.${format}`
    let skipMaterials = true
    let loaded: AssetContainer
    try {
      if (budget.limit) throw budget.limit
      if (format === 'glb' || format === 'gltf') {
        await prepareGltf(scan)
        const binary = data.byteLength >= 4 && new DataView(data).getUint32(0, true) === GLB_MAGIC
        pluginExtension = binary ? '.glb' : '.gltf'
        source = binary ? new Uint8Array(data) : `data:${new TextDecoder().decode(data)}`
      } else if (format === 'stl') {
        await Promise.all([import('@babylonjs/loaders/STL/stlFileLoader'), applyNeutralMaterial()])
        source = new File([data], fileName || 'model.stl')
      } else {
        const [obj] = await Promise.all([prepareObj(data, budget), applyNeutralMaterial()])
        source = new File([obj.source], fileName || 'model.obj')
        skipMaterials = obj.skipMaterials
      }
      if (disposed) throw abortError()

      try {
        loaded = await LoadAssetContainerAsync(source, scene, {
          pluginExtension,
          name: fileName,
          rootUrl: '',
          pluginOptions: {
            gltf: gltfOptions(scan, budget),
            // `encoding: 'auto'` is the decoding prepareObj mirrored; with nothing resolved there is no material
            // library to load, so the loader is told not to look for one at all.
            obj: { materialLoadingFailsSilently: true, skipMaterials, encoding: 'auto' },
          } as never,
        })
      } catch (error) {
        if (budget.limit) throw budget.limit
        // Babylon prefixes "Unable to load from <source>: " (the source being the whole JSON for glTF from memory).
        const what =
          typeof source === 'string' ? source
          : source instanceof File ? `file:${source.name}`
          : 'binary data'
        const message = error instanceof Error ? error.message : String(error)
        const prefix = `Unable to load from ${what}: `
        throw new Error(message.startsWith(prefix) ? message.slice(prefix.length) : message, { cause: error })
      }
      if (budget.limit) {
        loaded.dispose()
        throw budget.limit
      }
    } catch (error) {
      // Nothing of a failed load keeps downloading.
      budget.cancel()
      throw error
    }
    if (disposed) {
      loaded.dispose()
      throw abortError()
    }
    container = loaded
    // STL normals are frequently zero or wrong (exporters leave them out); recompute flat ones from the geometry.
    if (format === 'stl')
      for (const mesh of loaded.meshes) if (mesh.getTotalVertices() > 0) (mesh as Mesh).createNormals?.(false)
    loaded.addAllToScene()

    const stats = computeStats()
    if (!stats.meshes) throw new ModelUnsupportedError('This model has no geometry to show.')
    const min = Vector3.FromArray(stats.bounds.min)
    const max = Vector3.FromArray(stats.bounds.max)
    center = min.add(max).scale(0.5)
    radius = Math.max(max.subtract(min).length() / 2, 1e-6)
    farReach = radius
    buildGrid(min, max)
    frame(true)
    return stats
  }

  return {
    load,
    fit: () => frame(false),
    reset: () => frame(true),
    setGrid: visible => {
      gridVisible = visible
      grid?.setEnabled(visible)
    },
    setWireframe: enabled => {
      scene.forceWireframe = enabled
    },
    onContextLost: handler => {
      contextLost = handler
    },
    dispose: () => {
      if (disposed) return
      disposed = true
      observer?.disconnect()
      canvas.removeEventListener('webglcontextlost', onContextLost)
      engine.stopRenderLoop()
      camera.detachControl()
      container?.dispose()
      scene.dispose()
      engine.dispose()
      lifetime.abort(abortError())
      for (const { url, release } of objectUrls) {
        release()
        URL.revokeObjectURL(url)
      }
      objectUrls.length = 0
    },
  }
}
