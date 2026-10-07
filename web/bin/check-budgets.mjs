#!/usr/bin/env node
// Hard performance gate: first-load JavaScript per route (gzip) from a production `next build`.
// First load = the framework's root chunks + every entry chunk of the route's layouts and page (dynamic imports are
// excluded, so lazy-loading keeps a route inside its budget). Budgets live in perf-budgets.json.
//
// Two guards keep the 3D model viewer (Babylon.js) out of every route's first load:
// 1. no route's first-load set may contain Babylon code (content markers below);
// 2. the lazy chunks of the model viewer (the chunk carrying MODEL_VIEWER_MARKER, the chunks loaded with it and
//    everything they load on demand, minus what routes already load) have their own budget, `lazy.modelViewer`.
import fs from 'node:fs'
import path from 'node:path'
import zlib from 'node:zlib'
import vm from 'node:vm'

const webDir = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..')
const nextDir = path.join(webDir, '.next')
const budgets = JSON.parse(fs.readFileSync(path.join(webDir, 'perf-budgets.json'), 'utf8'))

// `MODEL_VIEWER_MARKER` in src/features/files/preview/ModelViewer.tsx.
const MODEL_VIEWER_MARKER = 'vh-model-viewer'
// Strings minified Babylon keeps: class registrations ("BABYLON.Mesh"), module paths (in its own error messages and
// non-minified output; with the trailing path so the shell's bundled package.json dependency list doesn't match),
// the camera class name the viewer always uses, and its CDN/doc URLs.
const BABYLON_MARKERS = ['BABYLON.', '@babylonjs/core/', '@babylonjs/loaders/', 'ArcRotateCamera', 'babylonjs.com']

if (!fs.existsSync(path.join(nextDir, 'build-manifest.json'))) {
  console.error('[budgets] no production build found: run `pnpm build` first')
  process.exit(2)
}

const buildManifest = JSON.parse(fs.readFileSync(path.join(nextDir, 'build-manifest.json'), 'utf8'))
// polyfillFiles load only in legacy (nomodule) browsers, so they don't count.
const root = [...(buildManifest.rootMainFiles ?? [])]
const gzCache = new Map()
const gz = file => {
  if (!gzCache.has(file)) gzCache.set(file, zlib.gzipSync(fs.readFileSync(path.join(nextDir, file)), { level: 9 }).length)
  return gzCache.get(file)
}
const textCache = new Map()
const text = file => {
  if (!textCache.has(file)) textCache.set(file, fs.readFileSync(path.join(nextDir, file), 'utf8'))
  return textCache.get(file)
}
const babylonMarker = file => BABYLON_MARKERS.find(marker => text(file).includes(marker))

const findFiles = (dir, name) => {
  const found = []
  const walk = d => {
    for (const entry of fs.readdirSync(d, { withFileTypes: true })) {
      const full = path.join(d, entry.name)
      if (entry.isDirectory()) walk(full)
      else if (entry.name === name) found.push(full)
    }
  }
  if (fs.existsSync(dir)) walk(dir)
  return found
}
const manifests = findFiles(path.join(nextDir, 'server', 'app'), 'page_client-reference-manifest.js')

const rows = []
const firstLoad = new Set(root)
const leaks = []
for (const file of manifests) {
  const sandbox = { self: {}, globalThis: {} }
  sandbox.globalThis = sandbox
  vm.runInNewContext(fs.readFileSync(file, 'utf8'), sandbox)
  const manifest = sandbox.__RSC_MANIFEST ?? sandbox.self.__RSC_MANIFEST
  for (const [key, value] of Object.entries(manifest)) {
    const route = key.replace(/\/page$/, '').replace(/\/\([^)]+\)/g, '') || '/'
    if (route.startsWith('/_') || route.startsWith('/api')) continue
    const files = new Set(root)
    for (const list of Object.values(value.entryJSFiles ?? {})) for (const f of list) files.add(f)
    const js = [...files].filter(f => f.endsWith('.js'))
    for (const f of js) firstLoad.add(f)
    const babylon = js.filter(f => babylonMarker(f))
    if (babylon.length)
      leaks.push(`${route}: ${babylon.length} chunk(s), e.g. ${babylon[0]} contains "${babylonMarker(babylon[0])}"`)
    const bytes = js.reduce((sum, f) => sum + gz(f), 0)
    const budget = budgets.routes?.[route] ?? budgets.default
    rows.push({ route, kb: bytes / 1024, budgetKb: budget, ok: bytes / 1024 <= budget })
  }
}

rows.sort((a, b) => a.route.localeCompare(b.route))
const width = Math.max(...rows.map(r => r.route.length), 5)
console.log(`${'route'.padEnd(width)}  first-load JS (gzip)  budget`)
for (const r of rows) console.log(`${r.route.padEnd(width)}  ${r.kb.toFixed(1).padStart(8)} KB          ${String(r.budgetKb).padStart(4)} KB  ${r.ok ? 'ok' : 'OVER'}`)
const over = rows.filter(r => !r.ok)
if (!rows.length) {
  console.error('[budgets] no routes found in the build output')
  process.exit(2)
}

// ------------------------------------------------------------------------------------- model viewer (lazy) guard
const failures = []
if (leaks.length) failures.push(`Babylon code in a route's first load (import ModelViewer only through next/dynamic):\n  ${leaks.join('\n  ')}`)

const chunkDir = path.join(nextDir, 'static', 'chunks')
const allChunks = fs
  .readdirSync(chunkDir, { recursive: true })
  .filter(f => String(f).endsWith('.js'))
  .map(f => `static/chunks/${String(f).split(path.sep).join('/')}`)
const viewerChunks = allChunks.filter(f => text(f).includes(MODEL_VIEWER_MARKER))
const babylonChunks = allChunks.filter(f => babylonMarker(f))

if (!viewerChunks.length) {
  if (babylonChunks.length)
    failures.push(
      `Babylon is in the build (${babylonChunks.length} chunks) but no chunk carries the "${MODEL_VIEWER_MARKER}" marker: ` +
        'import @babylonjs/* only from src/features/files/preview/ModelViewer.tsx and ./model/**',
    )
  else console.log(`[budgets] model viewer: not in this build (no chunk carries "${MODEL_VIEWER_MARKER}"), lazy budget skipped`)
} else {
  // Chunks loaded together with the viewer: every next/dynamic entry (react-loadable-manifest) that includes it.
  const initial = new Set(viewerChunks)
  for (const file of findFiles(path.join(nextDir, 'server', 'app'), 'react-loadable-manifest.json')) {
    for (const entry of Object.values(JSON.parse(fs.readFileSync(file, 'utf8')))) {
      const files = (entry.files ?? []).filter(f => f.endsWith('.js'))
      if (files.some(f => viewerChunks.includes(f))) for (const f of files) initial.add(f)
    }
  }
  // Turbopack async loaders also name a lazy group as one array of "static/chunks/<file>.js" strings (a nested
  // next/dynamic inside an already-lazy component has no react-loadable-manifest entry of its own): every chunk listed
  // next to the viewer's marker chunk loads with it.
  for (const file of allChunks) {
    for (const [array] of text(file).matchAll(/\[(?:"static\/chunks\/[^"]+?\.js",?)+\]/g)) {
      const listed = [...array.matchAll(/static\/chunks\/[^"]+?\.js/g)].map(m => m[0])
      if (listed.some(f => viewerChunks.includes(f))) for (const f of listed) initial.add(f)
    }
  }
  // Plus everything those chunks can load on demand (format loaders, glTF extensions, decoders): Turbopack's async
  // loaders name their chunks as "static/chunks/<file>.js" strings.
  const group = new Set(initial)
  const queue = [...initial]
  while (queue.length) {
    const file = queue.pop()
    for (const [ref] of text(file).matchAll(/static\/chunks\/[A-Za-z0-9._~\/-]+?\.js/g)) {
      if (!group.has(ref) && fs.existsSync(path.join(nextDir, ref))) {
        group.add(ref)
        queue.push(ref)
      }
    }
  }
  const lazy = file => !firstLoad.has(file)
  const sum = files => [...files].filter(lazy).reduce((total, f) => total + gz(f), 0) / 1024
  const initialKb = sum(initial)
  const totalKb = sum(group)
  const stray = babylonChunks.filter(f => !group.has(f) && !firstLoad.has(f))
  if (stray.length)
    failures.push(
      `${stray.length} Babylon chunk(s) outside the model viewer's lazy group (another import path reaches Babylon), ` +
        `e.g. ${stray.slice(0, 3).join(', ')}`,
    )
  // `modelViewer`: what opening the viewer downloads before any format loader (the next/dynamic chunk group).
  // `modelViewerReachable`: everything the viewer can ever load on demand. Turbopack copies shared modules into several
  // async chunks and Babylon ships WebGPU (WGSL) shader variants the WebGL viewer never fetches, so this over-counts any
  // single session; it is a ceiling against accidental growth (a barrel import, a new heavy dependency).
  const checks = [
    ['on open', initialKb, budgets.lazy?.modelViewer],
    ['reachable on demand', totalKb, budgets.lazy?.modelViewerReachable],
  ]
  for (const [label, kb, budget] of checks) {
    const fits = budget === undefined || kb <= budget
    console.log(`[budgets] model viewer (lazy, ${label}): ${kb.toFixed(1).padStart(7)} KB  budget ${budget ?? '—'} KB  ${fits ? 'ok' : 'OVER'}`)
    if (!fits) failures.push(`model viewer (${label}) is ${kb.toFixed(1)} KB gzip, over its ${budget} KB budget`)
  }
}

if (over.length) failures.unshift(`${over.length} route(s) over budget: ${over.map(r => r.route).join(', ')}`)
if (failures.length) {
  for (const failure of failures) console.error(`[budgets] ${failure}`)
  process.exit(1)
}
console.log(`[budgets] all ${rows.length} routes within budget`)
