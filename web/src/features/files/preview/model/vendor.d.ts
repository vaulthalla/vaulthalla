// The Emscripten glTF Draco decoder from `draco3dgltf` ships without typings. Its factory takes Emscripten module
// overrides (we pass `wasmBinary`) and resolves to the decoder module.
declare module 'draco3dgltf/draco_decoder_gltf_nodejs.js' {
  const DracoDecoderModule: (overrides?: { wasmBinary?: ArrayBuffer }) => Promise<unknown>
  export default DracoDecoderModule
}
