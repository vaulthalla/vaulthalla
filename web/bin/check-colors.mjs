#!/usr/bin/env node
// Design-token guard: feature and page code must use the theme tokens (text-fg-muted, bg-surface-2, text-ok, ...),
// never raw Tailwind palette colors or arbitrary color values. Primitives in src/components/ui may use them.
import fs from 'node:fs'
import path from 'node:path'

const webDir = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..')
const ROOTS = ['src/features', 'src/app', 'src/components/shell']
const PALETTE =
  'slate|gray|zinc|neutral|stone|red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose'
const UTIL = 'text|bg|border|border-[trblxy]|from|to|via|ring|ring-offset|shadow|fill|stroke|outline|divide|placeholder|decoration|caret|accent'
const rules = [
  { re: new RegExp(`\\b(?:${UTIL})-(?:${PALETTE})-\\d{2,3}(?:/\\d+)?\\b`, 'g'), why: 'raw palette color (use a theme token)' },
  { re: new RegExp(`\\b(?:text|bg|border|divide|ring)-white(?:/\\d+)?\\b`, 'g'), why: 'raw white (use fg/surface/line tokens)' },
  { re: /\b[a-z-]+-\[(?:#|rgb|rgba|hsl|oklch)[^\]]*\]/g, why: 'arbitrary color value (add a token)' },
]

const offenders = []
const walk = dir => {
  if (!fs.existsSync(dir)) return
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name)
    if (entry.isDirectory()) walk(full)
    else if (/\.(tsx?|jsx?)$/.test(entry.name)) {
      const lines = fs.readFileSync(full, 'utf8').split('\n')
      lines.forEach((line, index) => {
        for (const rule of rules)
          for (const match of line.matchAll(rule.re)) offenders.push(`${path.relative(webDir, full)}:${index + 1}  ${match[0]}  — ${rule.why}`)
      })
    }
  }
}
for (const root of ROOTS) walk(path.join(webDir, root))

if (offenders.length) {
  console.error(offenders.join('\n'))
  console.error(`[colors] ${offenders.length} raw color use(s) outside src/components/ui`)
  process.exit(1)
}
console.log('[colors] feature code uses theme tokens only')
