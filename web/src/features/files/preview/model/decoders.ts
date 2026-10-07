// Compressed-glTF decoders, shipped with the console instead of fetched from Babylon's CDN (self-hosted installs are
// often offline, and the console must never call third-party hosts). This module is imported dynamically, only when
// a model's `extensionsUsed` lists one of these extensions, so neither decoder costs anything for other models.
//
// - Draco (KHR_draco_mesh_compression): the Emscripten module from `draco3dgltf` (Apache-2.0). Its wasm binary is
//   emitted by the bundler under /_next/static (never web/public: the middleware would send anonymous share visitors
//   to /login), fetched same-origin, and handed to Babylon with `numWorkers: 0` so nothing loads by URL or blob worker.
// - meshopt (EXT_meshopt_compression): `meshoptimizer/decoder` (MIT) embeds its wasm, so it is plain bundled JS.
//   Babylon expects a global `MeshoptDecoder` loaded by a <script> from its CDN; we pre-seed its default instance
//   with the bundled decoder instead, decoding on the main thread (fast enough, and no blob workers).
// - KTX2/Basis textures (KHR_texture_basisu) are not shipped (several MB of transcoders): the viewer disables the
//   extension so the loader falls back to the PNG/JPEG source, and refuses models that *require* it.

import { DracoDecoder } from '@babylonjs/core/Meshes/Compression/dracoDecoder'
import { MeshoptCompression } from '@babylonjs/core/Meshes/Compression/meshoptCompression'

let draco: Promise<void> | null = null
let meshopt: Promise<void> | null = null

export const ensureDraco = () => {
  draco ??= (async () => {
    const wasmUrl = new URL('draco3dgltf/draco_decoder_gltf.wasm', import.meta.url)
    const scriptUrl = new URL('draco3dgltf/draco_decoder_gltf_nodejs.js', import.meta.url)
    const wasm = await fetch(wasmUrl, { credentials: 'same-origin' }).then(response => {
      if (!response.ok) throw new Error(`Draco decoder unavailable (${response.status})`)
      return response.arrayBuffer()
    })
    DracoDecoder.ResetDefault()
    DracoDecoder.DefaultConfiguration = {
      wasmUrl: scriptUrl.href,
      wasmBinaryUrl: wasmUrl.href,
      wasmBinary: wasm,
      numWorkers: 0,
    }
  })().catch(error => {
    draco = null
    throw error
  })
  return draco
}

interface MeshoptDecoderApi {
  ready: Promise<void>
  supported: boolean
  decodeGltfBuffer: (
    target: Uint8Array,
    count: number,
    size: number,
    source: Uint8Array,
    mode: string,
    filter?: string,
  ) => void
}

export const ensureMeshopt = () => {
  meshopt ??= (async () => {
    const { MeshoptDecoder } = (await import('meshoptimizer/decoder')) as { MeshoptDecoder: MeshoptDecoderApi }
    await MeshoptDecoder.ready
    if (!MeshoptDecoder.supported) throw new Error('This browser cannot run the meshopt decoder (WebAssembly).')
    // Babylon reads the global by name and drives it through useWorkers/decodeGltfBufferAsync: give it a
    // main-thread shim so no blob: worker is ever created.
    const shim = {
      ready: MeshoptDecoder.ready,
      supported: true,
      useWorkers: () => {},
      decodeGltfBufferAsync: async (count: number, size: number, source: Uint8Array, mode: string, filter?: string) => {
        const target = new Uint8Array(count * size)
        MeshoptDecoder.decodeGltfBuffer(target, count, size, source, mode, filter)
        return target
      },
    }
    ;(globalThis as { MeshoptDecoder?: unknown }).MeshoptDecoder = shim
    const seeded = Object.create(MeshoptCompression.prototype) as MeshoptCompression
    ;(seeded as unknown as { _decoderModulePromise: Promise<void> })._decoderModulePromise = MeshoptDecoder.ready
    ;(MeshoptCompression as unknown as { _Default: MeshoptCompression | null })._Default = seeded
  })().catch(error => {
    meshopt = null
    throw error
  })
  return meshopt
}
