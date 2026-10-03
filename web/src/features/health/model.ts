// Health data helpers. The backend owns every severity; nothing here invents one. Missing values stay null so
// the UI renders them as "not available".

import { formatMoney } from '@/lib/format'
import { severityTone, type Tone } from '@/lib/tone'

export type Severity = 'healthy' | 'info' | 'warning' | 'error' | 'unknown' | 'unavailable'

export interface Metric {
  key: string
  label: string
  value: string
  unit: string | null
  tone: Severity
  numeric: number | null
  href: string | null
}

export interface SeriesPoint {
  t: number
  v: number
}

export interface Series {
  key: string
  label: string
  unit: string
  points: SeriesPoint[]
}

export interface Issue {
  code: string
  severity: Severity
  message: string
  href: string | null
  cardId: string | null
  title: string | null
}

export interface Card {
  id: string
  sectionId: string
  title: string
  description: string
  href: string
  variant: string
  size: string
  severity: Severity
  available: boolean
  unavailableReason: string | null
  summary: string
  metrics: Metric[]
  series: Series[]
  warnings: Issue[]
  errors: Issue[]
  checkedAt: number | string | null
}

export interface Section {
  id: string
  title: string
  href: string
  severity: Severity
  warningCount: number | null
  errorCount: number | null
  summary: string
}

export interface Overview {
  overallStatus: Severity
  warningCount: number | null
  errorCount: number | null
  checkedAt: number | string | null
  attention: Issue[]
  sections: Section[]
  cards: Card[]
}

type Obj = Record<string, unknown>
const obj = (v: unknown): Obj => (v && typeof v === 'object' && !Array.isArray(v) ? (v as Obj) : {})
const str = (v: unknown, fallback = '') => (typeof v === 'string' ? v : fallback)
const strOrNull = (v: unknown) => (typeof v === 'string' && v ? v : null)
const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : null)
const arr = (v: unknown) => (Array.isArray(v) ? v : [])
const stamp = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : typeof v === 'string' && v ? v : null)

export const asSeverity = (v: unknown): Severity =>
  v === 'healthy' || v === 'info' || v === 'warning' || v === 'error' || v === 'unavailable' ? v : 'unknown'

const parseIssue = (raw: unknown): Issue => {
  const d = obj(raw)
  return {
    code: str(d.code),
    severity: asSeverity(d.severity),
    message: str(d.message),
    href: strOrNull(d.href),
    cardId: strOrNull(d.card_id),
    title: strOrNull(d.title),
  }
}

const parseMetric = (raw: unknown): Metric => {
  const d = obj(raw)
  return {
    key: str(d.key),
    label: str(d.label),
    value: str(d.value),
    unit: strOrNull(d.unit),
    tone: asSeverity(d.tone),
    numeric: num(d.numeric_value),
    href: strOrNull(d.href),
  }
}

const parseSeries = (raw: unknown): Series => {
  const d = obj(raw)
  return {
    key: str(d.key),
    label: str(d.label),
    unit: str(d.unit),
    points: arr(d.points)
      .map(p => ({ t: num(obj(p).created_at), v: num(obj(p).value) }))
      .filter((p): p is SeriesPoint => p.t !== null && p.v !== null),
  }
}

export const parseCard = (raw: unknown): Card => {
  const d = obj(raw)
  return {
    id: str(d.id),
    sectionId: str(d.section_id),
    title: str(d.title),
    description: str(d.description),
    href: str(d.href),
    variant: str(d.variant),
    size: str(d.size),
    severity: asSeverity(d.severity),
    // Only an explicit `true` counts as available; a missing flag is not a healthy card.
    available: d.available === true,
    unavailableReason: strOrNull(d.unavailable_reason),
    summary: str(d.summary),
    metrics: arr(d.metrics).map(parseMetric),
    series: arr(d.series).map(parseSeries),
    warnings: arr(d.warnings).map(parseIssue),
    errors: arr(d.errors).map(parseIssue),
    checkedAt: stamp(d.checked_at),
  }
}

export const parseOverview = (raw: unknown): Overview => {
  const d = obj(raw)
  return {
    overallStatus: asSeverity(d.overall_status),
    warningCount: num(d.warning_count),
    errorCount: num(d.error_count),
    checkedAt: stamp(d.checked_at),
    attention: arr(d.attention).map(parseIssue),
    sections: arr(d.sections).map(raw => {
      const s = obj(raw)
      return {
        id: str(s.id),
        title: str(s.title),
        href: str(s.href),
        severity: asSeverity(s.severity),
        warningCount: num(s.warning_count),
        errorCount: num(s.error_count),
        summary: str(s.summary),
      }
    }),
    cards: arr(d.cards).map(parseCard),
  }
}

export const SEVERITY_RANK: Record<Severity, number> = { error: 5, warning: 4, unknown: 3, info: 2, unavailable: 1, healthy: 0 }

export const sortIssues = <T extends { severity: Severity }>(issues: T[]) =>
  [...issues].sort((a, b) => SEVERITY_RANK[b.severity] - SEVERITY_RANK[a.severity])

export const severityText = (severity: Severity | string | null | undefined) => {
  switch (severity) {
    case 'healthy':
      return 'Healthy'
    case 'info':
      return 'Info'
    case 'warning':
      return 'Warning'
    case 'error':
      return 'Error'
    case 'unavailable':
      return 'Not available'
    default:
      return 'Unknown'
  }
}

// Mirrors core's dashboardOverviewSeverityFromStatus so the raw status words on detail payloads (pool `idle`,
// cleanup `overdue`, ...) get the same tone the backend overview gives them. Anything else is unknown.
export const statusTone = (status: string | null | undefined): Tone => {
  switch (status) {
    case 'healthy':
    case 'idle':
    case 'normal':
    case 'ready':
      return 'ok'
    case 'info':
    case 'syncing':
    case 'success':
      return 'info'
    case 'warning':
    case 'degraded':
    case 'pressured':
    case 'stale':
      return 'warn'
    case 'error':
    case 'critical':
    case 'saturated':
    case 'failing':
    case 'stalled':
    case 'diverged':
    case 'overdue':
      return 'danger'
    default:
      return severityTone(status)
  }
}

export const issueCountText = (errors: number | null, warnings: number | null) => {
  const parts: string[] = []
  if (errors) parts.push(`${errors} error${errors === 1 ? '' : 's'}`)
  if (warnings) parts.push(`${warnings} warning${warnings === 1 ? '' : 's'}`)
  return parts.join(' · ')
}

// The backend still links to the old console routes. Map them onto /health (and /cost) so drilldowns work against
// both old and new daemons.
export const healthHref = (href: string | null | undefined, fallback = '/health'): string => {
  if (!href) return fallback
  const [path, hash] = href.split('#')
  const suffix = hash ? `#${hash}` : ''
  if (path === '/dashboard' || path === '/dashboard/') return `/health${suffix}`
  if (path.startsWith('/dashboard/operations') || path.startsWith('/dashboard/trends')) return `/health/activity${suffix}`
  if (path.startsWith('/dashboard/')) return `/health/${path.slice('/dashboard/'.length)}${suffix}`
  if (path.startsWith('/pricing-budget')) return `/cost${suffix}`
  return href
}

const UNKNOWN_WORDS = new Set(['', 'unknown', 'n/a', 'not available', 'unavailable'])

// A metric's display string. Unknown words become null ("not available"); money is reformatted (the backend sends
// catalog precision, e.g. "0.00000000 USD").
export const metricDisplay = (metric: Pick<Metric, 'value'>): string | null => {
  const value = metric.value.trim()
  if (UNKNOWN_WORDS.has(value.toLowerCase())) return null
  const money = /^(-?\d+(?:\.\d+)?)\s+([A-Z]{3})$/.exec(value)
  if (money) return formatMoney(money[1], money[2])
  return value
}

// Numeric reading of a metric: the backend's numeric_value, else a plain number in the display string.
export const metricNumber = (metric: Metric): number | null => {
  if (metric.numeric !== null) return metric.numeric
  const plain = /^-?\d+(?:\.\d+)?$/.exec(metric.value.trim().replace(/,/g, ''))
  return plain ? Number(plain[0]) : null
}

// Only warnings and errors get color on a value; healthy/info values read in plain ink, and a value the backend
// couldn't measure never inherits a tone.
export const metricTone = (metric: Metric): Tone | undefined => {
  if (metricDisplay(metric) === null) return undefined
  if (metric.tone === 'warning') return 'warn'
  if (metric.tone === 'error') return 'danger'
  return undefined
}
