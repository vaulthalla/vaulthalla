#!/usr/bin/env node
// Hard performance gate: first-load JavaScript per route (gzip) from a production `next build`.
// First load = the framework's root chunks + every entry chunk of the route's layouts and page (dynamic imports are
// excluded, so lazy-loading keeps a route inside its budget). Budgets live in perf-budgets.json.
import fs from 'node:fs'
import path from 'node:path'
import zlib from 'node:zlib'
import vm from 'node:vm'

const webDir = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..')
const nextDir = path.join(webDir, '.next')
const budgets = JSON.parse(fs.readFileSync(path.join(webDir, 'perf-budgets.json'), 'utf8'))

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

const manifests = []
const walk = dir => {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) walk(full)
    else if (entry.name === 'page_client-reference-manifest.js') manifests.push(full)
  }
}
walk(path.join(nextDir, 'server', 'app'))

const rows = []
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
    const bytes = [...files].filter(f => f.endsWith('.js')).reduce((sum, f) => sum + gz(f), 0)
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
if (over.length) {
  console.error(`[budgets] ${over.length} route(s) over budget: ${over.map(r => r.route).join(', ')}`)
  process.exit(1)
}
console.log(`[budgets] all ${rows.length} routes within budget`)
