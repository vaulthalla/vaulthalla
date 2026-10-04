// Display formatting. Every formatter takes `null`/`undefined` and renders it as unknown ("—" by default), never as 0.

export const DASH = '—'

const finite = (value: unknown): number | null => {
  if (typeof value === 'number') return Number.isFinite(value) ? value : null
  if (typeof value === 'string' && value.trim() !== '') {
    const parsed = Number(value)
    return Number.isFinite(parsed) ? parsed : null
  }
  return null
}

const UNITS = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']

export const formatBytes = (value: unknown, fallback = DASH): string => {
  const bytes = finite(value)
  if (bytes === null || bytes < 0) return fallback
  if (bytes < 1024) return `${Math.round(bytes)} B`
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), UNITS.length - 1)
  const scaled = bytes / 1024 ** i
  return `${scaled.toFixed(scaled < 10 ? 2 : scaled < 100 ? 1 : 0)} ${UNITS[i]}`
}

const integer = new Intl.NumberFormat(undefined, { maximumFractionDigits: 0 })
const compact = new Intl.NumberFormat(undefined, { notation: 'compact', maximumFractionDigits: 1 })

export const formatInt = (value: unknown, fallback = DASH) => {
  const n = finite(value)
  return n === null ? fallback : integer.format(n)
}

export const formatCompact = (value: unknown, fallback = DASH) => {
  const n = finite(value)
  return n === null ? fallback : compact.format(n)
}

// `ratio` is 0..1 unless `isPercent` (already 0..100).
export const formatPercent = (value: unknown, { isPercent = false, digits = 1, fallback = DASH } = {}) => {
  const n = finite(value)
  if (n === null) return fallback
  return `${(isPercent ? n : n * 100).toFixed(digits)}%`
}

// Money arrives as decimal strings with catalog precision (e.g. "0.00000000"). Show cents unless the amount is
// smaller than a cent, then enough significant digits to be honest about it.
export const formatMoney = (value: unknown, currency = 'USD', fallback = DASH) => {
  const n = finite(value)
  if (n === null) return fallback
  const abs = Math.abs(n)
  const digits = abs === 0 || abs >= 0.01 ? 2 : Math.min(8, Math.max(2, -Math.floor(Math.log10(abs)) + 2))
  try {
    return new Intl.NumberFormat(undefined, {
      style: 'currency',
      currency,
      minimumFractionDigits: digits,
      maximumFractionDigits: digits,
    }).format(n)
  } catch {
    return `${n.toFixed(digits)} ${currency}`
  }
}

// Accepts unix seconds, unix milliseconds, numeric strings and ISO strings.
export const parseDate = (value: unknown): Date | null => {
  if (value instanceof Date) return Number.isNaN(value.getTime()) ? null : value
  const n = finite(value)
  if (n !== null) return n > 0 ? new Date(n < 1_000_000_000_000 ? n * 1000 : n) : null
  if (typeof value !== 'string' || !value.trim()) return null
  const parsed = new Date(value)
  return Number.isNaN(parsed.getTime()) ? null : parsed
}

const dateTime = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short' })
const dateOnly = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' })
const timeOnly = new Intl.DateTimeFormat(undefined, { timeStyle: 'medium' })

export const formatDateTime = (value: unknown, fallback = DASH) => {
  const d = parseDate(value)
  return d ? dateTime.format(d) : fallback
}

export const formatDate = (value: unknown, fallback = DASH) => {
  const d = parseDate(value)
  return d ? dateOnly.format(d) : fallback
}

export const formatTime = (value: unknown, fallback = DASH) => {
  const d = parseDate(value)
  return d ? timeOnly.format(d) : fallback
}

const relative = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto' })
const STEPS: [Intl.RelativeTimeFormatUnit, number][] = [
  ['year', 31_536_000],
  ['month', 2_592_000],
  ['week', 604_800],
  ['day', 86_400],
  ['hour', 3_600],
  ['minute', 60],
]

export const formatRelative = (value: unknown, fallback = DASH, now = Date.now()) => {
  const d = parseDate(value)
  if (!d) return fallback
  const seconds = Math.round((d.getTime() - now) / 1000)
  for (const [unit, size] of STEPS) if (Math.abs(seconds) >= size) return relative.format(Math.round(seconds / size), unit)
  return Math.abs(seconds) < 10 ? 'just now' : relative.format(seconds, 'second')
}

export const formatDuration = (seconds: unknown, fallback = DASH) => {
  const s = finite(seconds)
  if (s === null || s < 0) return fallback
  if (s < 1) return `${Math.round(s * 1000)} ms`
  if (s < 60) return `${s.toFixed(s < 10 ? 1 : 0)} s`
  const m = Math.floor(s / 60)
  if (m < 60) return `${m} min${s % 60 >= 1 ? ` ${Math.floor(s % 60)} s` : ''}`
  const h = Math.floor(m / 60)
  if (h < 48) return `${h} h${m % 60 ? ` ${m % 60} min` : ''}`
  return `${Math.floor(h / 24)} d${h % 24 ? ` ${h % 24} h` : ''}`
}

export const titleCase = (value: string) =>
  value
    .replace(/[_.-]+/g, ' ')
    .trim()
    .replace(/\b\w/g, c => c.toUpperCase())
