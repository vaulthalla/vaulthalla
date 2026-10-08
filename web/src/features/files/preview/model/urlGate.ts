// The engine-wide network gate for the model viewer. A model is untrusted input: whatever URL it manages to hand
// Babylon (an OBJ `mtllib`, an MTL texture, a glTF buffer or image, an extension's own URI) must never make the
// viewer's browser fetch it. The format-specific code already resolves every reference it knows about to an object URL;
// this gate is the backstop for the ones it doesn't. Every URL Babylon is about to load goes through one of three
// hooks, and each one only lets through `data:` URLs (decoded locally, never on the network) and the exact `blob:` or
// same-origin URLs the viewer registered with `allowUrl`. Anything else is swapped for a malformed `data:` URL, so the
// load fails locally (an image error, an empty material) without a request ever leaving the page.
//
// - FileToolsOptions.PreprocessUrl (Tools.PreprocessUrl): every LoadImage/LoadFile/RequestFile string URL (textures,
//   MTL files, glTF buffers and images after the glTF loader's own preprocessUrlAsync, environment assets).
// - FileToolsOptions.ScriptPreprocessUrl: every Babylon script/wasm URL (decoders, transcoders, the glTF validator).
// - WebRequest.CustomRequestModifiers: every XHR/fetch Babylon makes through WebRequest, including the ones that skip
//   the two hooks above. SkipRequestModificationForBabylonCDN is turned off so CDN requests are gated too.
//
// Babylon is used by nothing else in the console, so its globals are the viewer's to set.

import { Tools } from '@babylonjs/core/Misc/tools'
import { WebRequest } from '@babylonjs/core/Misc/webRequest'

/** What a refused URL becomes: invalid base64, so XHR, fetch, <img> and <script> all fail without a request. */
export const BLOCKED_URL = 'data:application/x-vaulthalla-blocked;base64,!'

const allowed = new Map<string, number>()
let installed = false
let warned = 0

const absolute = (url: string) => {
  try {
    return new URL(url, document.baseURI).href
  } catch {
    return url
  }
}

/**
 * Lets Babylon load this exact URL until the returned function is called (registrations are counted). A relative
 * URL (bundler asset URLs can be root-relative) is also allowed in its absolute form, which Babylon may load it as.
 */
export const allowUrl = (url: string): (() => void) => {
  const forms = [...new Set([url, absolute(url)])]
  for (const form of forms) allowed.set(form, (allowed.get(form) ?? 0) + 1)
  let released = false
  return () => {
    if (released) return
    released = true
    for (const form of forms) {
      const count = (allowed.get(form) ?? 1) - 1
      if (count > 0) allowed.set(form, count)
      else allowed.delete(form)
    }
  }
}

export const isAllowedUrl = (url: string) => url.startsWith('data:') || allowed.has(url)

/** Returns `url` if the viewer may load it, BLOCKED_URL otherwise. */
export const gateUrl = (url: string): string => {
  if (typeof url !== 'string') return BLOCKED_URL
  if (isAllowedUrl(url)) return url
  if (warned < 20) {
    warned++
    console.warn(`[model] refused to load ${url.slice(0, 120)}: the 3D viewer only loads files it resolved itself`)
  }
  return BLOCKED_URL
}

/** Installs the gate on Babylon's globals (idempotent; re-asserted on every viewer so nothing can have loosened it). */
export const installUrlGate = () => {
  Tools.PreprocessUrl = gateUrl
  Tools.ScriptPreprocessUrl = gateUrl
  // RequestFile prepends BaseUrl after preprocessing; the WebRequest hook would still catch it, but keep it empty.
  Tools.BaseUrl = ''
  WebRequest.SkipRequestModificationForBabylonCDN = false
  if (!installed) {
    installed = true
    WebRequest.CustomRequestModifiers.push((_request, url) => gateUrl(url))
  }
}
