// Pure helpers for the settings editor: path access and the merge patch that settings.update expects.

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json }
export type JsonObject = { [key: string]: Json }

export const isObject = (v: unknown): v is JsonObject => Boolean(v) && typeof v === 'object' && !Array.isArray(v)

export const getAt = (root: Json, path: string[]): Json | undefined => {
  let node: Json | undefined = root
  for (const key of path) {
    if (!isObject(node)) return undefined
    node = node[key]
  }
  return node
}

export const setAt = (root: JsonObject, path: string[], value: Json): JsonObject => {
  const [head, ...rest] = path
  const child = root[head]
  return { ...root, [head]: rest.length ? setAt(isObject(child) ? child : {}, rest, value) : value }
}

const same = (a: Json | undefined, b: Json | undefined) => JSON.stringify(a) === JSON.stringify(b)

// RFC 7386 merge patch from `before` to `after`: only changed leaves, arrays replaced whole. Core merges it onto
// the live config, so untouched sections are never sent (and never clobbered).
export const mergePatch = (before: JsonObject, after: JsonObject): JsonObject => {
  const patch: JsonObject = {}
  for (const key of Object.keys(after)) {
    const a = before[key]
    const b = after[key]
    if (isObject(a) && isObject(b)) {
      const sub = mergePatch(a, b)
      if (Object.keys(sub).length) patch[key] = sub
    } else if (!same(a, b)) {
      patch[key] = b
    }
  }
  return patch
}

// Leaf paths in a patch, e.g. ['s3_gateway.port', 'auth.access_token_expiry_minutes'].
export const leafPaths = (patch: JsonObject, prefix: string[] = []): string[][] =>
  Object.entries(patch).flatMap(([key, value]) =>
    isObject(value) ? leafPaths(value, [...prefix, key]) : [[...prefix, key]],
  )

// "50MB" / "1GB" ⇄ { amount, unit }
export const parseSize = (value: unknown) => {
  const m = typeof value === 'string' ? value.trim().match(/^(\d+)\s*(MB|GB|M|G)$/i) : null
  if (!m) return null
  return { amount: Number(m[1]), unit: m[2].toUpperCase().startsWith('G') ? 'GB' : 'MB' }
}

// "24h" / "1d" ⇄ { amount, unit }
export const parseInterval = (value: unknown) => {
  const m = typeof value === 'string' ? value.trim().match(/^(\d+)\s*([hd])$/i) : null
  if (!m) return null
  return { amount: Number(m[1]), unit: m[2].toLowerCase() as 'h' | 'd' }
}
