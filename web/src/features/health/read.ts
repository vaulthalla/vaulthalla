// Field readers for raw stats payloads. A missing or malformed field is null (rendered "not available"), never 0.

import { formatBytes, formatDuration, formatInt, formatPercent } from '@/lib/format'

export type Raw = Record<string, unknown>

export const obj = (v: unknown): Raw => (v && typeof v === 'object' && !Array.isArray(v) ? (v as Raw) : {})
export const list = (v: unknown): Raw[] => (Array.isArray(v) ? v.map(obj) : [])
export const num = (v: unknown): number | null => (typeof v === 'number' && Number.isFinite(v) ? v : null)
export const bool = (v: unknown): boolean | null => (typeof v === 'boolean' ? v : null)
export const text = (v: unknown): string | null => (typeof v === 'string' && v ? v : null)

export const int = (v: unknown) => (num(v) === null ? null : formatInt(v))
export const bytes = (v: unknown) => (num(v) === null ? null : formatBytes(v))
export const seconds = (v: unknown) => (num(v) === null ? null : formatDuration(v))
export const percent = (v: unknown, digits = 1) => (num(v) === null ? null : formatPercent(v, { digits }))
export const ms = (v: unknown) => {
  const n = num(v)
  return n === null ? null : `${n < 10 ? n.toFixed(1) : Math.round(n)} ms`
}
export const pair = (a: unknown, b: unknown, sep = ' / ') => (num(a) !== null && num(b) !== null ? `${formatInt(a)}${sep}${formatInt(b)}` : null)
