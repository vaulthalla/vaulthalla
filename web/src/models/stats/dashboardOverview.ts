export type DashboardSeverity = 'healthy' | 'info' | 'warning' | 'error' | 'unknown' | 'unavailable'

export interface DashboardOverviewCardRequest {
  id: string
  variant?: string
  size?: string
}

export interface DashboardOverviewRequest {
  scope?: 'system'
  mode?: string
  cards?: DashboardOverviewCardRequest[]
}

export interface IDashboardMetricSummary {
  key: string
  label: string
  value: string
  unit: string | null
  tone: DashboardSeverity
  numeric_value: number | null
  href: string | null
}

export interface IDashboardGraphPoint {
  created_at: number
  value: number
}

export interface IDashboardGraphSeries {
  key: string
  label: string
  unit: string
  tone: DashboardSeverity
  points: DashboardGraphPoint[]
}

export interface IDashboardIssueSummary {
  code: string
  severity: DashboardSeverity
  message: string
  href: string | null
  metric_key: string | null
}

export interface IDashboardAttentionItem extends IDashboardIssueSummary {
  card_id: string
  title: string
}

export interface IDashboardCardSummary {
  id: string
  section_id: string
  title: string
  description: string
  href: string
  variant: string
  size: string
  severity: DashboardSeverity
  available: boolean
  unavailable_reason: string | null
  summary: string
  metrics: DashboardMetricSummary[]
  series: DashboardGraphSeries[]
  warnings: DashboardIssueSummary[]
  errors: DashboardIssueSummary[]
  checked_at: number | string | null
}

export interface IDashboardSectionSummary {
  id: string
  title: string
  description: string
  href: string
  severity: DashboardSeverity
  warning_count: number
  error_count: number
  summary: string
  metrics: DashboardMetricSummary[]
  warnings: DashboardIssueSummary[]
  errors: DashboardIssueSummary[]
  checked_at: number | string | null
}

export interface IDashboardOverview {
  overall_status: DashboardSeverity
  warning_count: number
  error_count: number
  checked_at: number | string | null
  attention: DashboardAttentionItem[]
  sections: DashboardSectionSummary[]
  cards: DashboardCardSummary[]
}

function asObject(v: unknown): Record<string, unknown> {
  return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : {}
}

function asString(v: unknown, fallback = ''): string {
  return typeof v === 'string' ? v : fallback
}

function asNullableString(v: unknown): string | null {
  return typeof v === 'string' && v.length > 0 ? v : null
}

function asNumber(v: unknown, fallback = 0): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback
}

function asNullableNumber(v: unknown): number | null {
  return typeof v === 'number' && Number.isFinite(v) ? v : null
}

function asBoolean(v: unknown, fallback = false): boolean {
  return typeof v === 'boolean' ? v : fallback
}

function asCheckedAt(v: unknown): number | string | null {
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.length > 0) return v
  return null
}

function asSeverity(v: unknown): DashboardSeverity {
  return v === 'healthy' || v === 'info' || v === 'warning' || v === 'error' || v === 'unavailable' ?
      v
    : 'unknown'
}

export class DashboardMetricSummary implements IDashboardMetricSummary {
  key = ''
  label = ''
  value = ''
  unit: string | null = null
  tone: DashboardSeverity = 'unknown'
  numeric_value: number | null = null
  href: string | null = null

  constructor(data?: Partial<IDashboardMetricSummary>) {
    if (!data) return
    this.key = data.key ?? this.key
    this.label = data.label ?? this.label
    this.value = data.value ?? this.value
    this.unit = data.unit ?? this.unit
    this.tone = data.tone ?? this.tone
    this.numeric_value = data.numeric_value ?? this.numeric_value
    this.href = data.href ?? this.href
  }

  static from(raw: unknown): DashboardMetricSummary {
    const data = asObject(raw)
    return new DashboardMetricSummary({
      key: asString(data.key),
      label: asString(data.label),
      value: asString(data.value),
      unit: asNullableString(data.unit),
      tone: asSeverity(data.tone),
      numeric_value: asNullableNumber(data.numeric_value),
      href: asNullableString(data.href),
    })
  }
}

export class DashboardGraphPoint implements IDashboardGraphPoint {
  created_at = 0
  value = 0

  constructor(data?: Partial<IDashboardGraphPoint>) {
    if (!data) return
    this.created_at = data.created_at ?? this.created_at
    this.value = data.value ?? this.value
  }

  static from(raw: unknown): DashboardGraphPoint {
    const data = asObject(raw)
    return new DashboardGraphPoint({
      created_at: asNumber(data.created_at),
      value: asNumber(data.value),
    })
  }
}

export class DashboardGraphSeries implements IDashboardGraphSeries {
  key = ''
  label = ''
  unit = ''
  tone: DashboardSeverity = 'info'
  points: DashboardGraphPoint[] = []

  constructor(data?: Partial<IDashboardGraphSeries>) {
    if (!data) return
    this.key = data.key ?? this.key
    this.label = data.label ?? this.label
    this.unit = data.unit ?? this.unit
    this.tone = data.tone ?? this.tone
    this.points = data.points ?? this.points
  }

  static from(raw: unknown): DashboardGraphSeries {
    const data = asObject(raw)
    return new DashboardGraphSeries({
      key: asString(data.key),
      label: asString(data.label),
      unit: asString(data.unit),
      tone: asSeverity(data.tone),
      points: Array.isArray(data.points) ? data.points.map(DashboardGraphPoint.from) : [],
    })
  }
}

export class DashboardIssueSummary implements IDashboardIssueSummary {
  code = ''
  severity: DashboardSeverity = 'unknown'
  message = ''
  href: string | null = null
  metric_key: string | null = null

  constructor(data?: Partial<IDashboardIssueSummary>) {
    if (!data) return
    this.code = data.code ?? this.code
    this.severity = data.severity ?? this.severity
    this.message = data.message ?? this.message
    this.href = data.href ?? this.href
    this.metric_key = data.metric_key ?? this.metric_key
  }

  static from(raw: unknown): DashboardIssueSummary {
    const data = asObject(raw)
    return new DashboardIssueSummary({
      code: asString(data.code),
      severity: asSeverity(data.severity),
      message: asString(data.message),
      href: asNullableString(data.href),
      metric_key: asNullableString(data.metric_key),
    })
  }
}

export class DashboardAttentionItem extends DashboardIssueSummary implements IDashboardAttentionItem {
  card_id = ''
  title = ''

  constructor(data?: Partial<IDashboardAttentionItem>) {
    super(data)
    if (!data) return
    this.card_id = data.card_id ?? this.card_id
    this.title = data.title ?? this.title
  }

  static override from(raw: unknown): DashboardAttentionItem {
    const data = asObject(raw)
    return new DashboardAttentionItem({
      code: asString(data.code),
      severity: asSeverity(data.severity),
      message: asString(data.message),
      href: asNullableString(data.href),
      metric_key: asNullableString(data.metric_key),
      card_id: asString(data.card_id),
      title: asString(data.title),
    })
  }
}

export class DashboardCardSummary implements IDashboardCardSummary {
  id = ''
  section_id = ''
  title = ''
  description = ''
  href = ''
  variant = 'tiles'
  size = '2x1'
  severity: DashboardSeverity = 'unknown'
  available = true
  unavailable_reason: string | null = null
  summary = ''
  metrics: DashboardMetricSummary[] = []
  series: DashboardGraphSeries[] = []
  warnings: DashboardIssueSummary[] = []
  errors: DashboardIssueSummary[] = []
  checked_at: number | string | null = null

  constructor(data?: Partial<IDashboardCardSummary>) {
    if (!data) return
    this.id = data.id ?? this.id
    this.section_id = data.section_id ?? this.section_id
    this.title = data.title ?? this.title
    this.description = data.description ?? this.description
    this.href = data.href ?? this.href
    this.variant = data.variant ?? this.variant
    this.size = data.size ?? this.size
    this.severity = data.severity ?? this.severity
    this.available = data.available ?? this.available
    this.unavailable_reason = data.unavailable_reason ?? this.unavailable_reason
    this.summary = data.summary ?? this.summary
    this.metrics = data.metrics ?? this.metrics
    this.series = data.series ?? this.series
    this.warnings = data.warnings ?? this.warnings
    this.errors = data.errors ?? this.errors
    this.checked_at = data.checked_at ?? this.checked_at
  }

  static from(raw: unknown): DashboardCardSummary {
    const data = asObject(raw)
    return new DashboardCardSummary({
      id: asString(data.id),
      section_id: asString(data.section_id),
      title: asString(data.title),
      description: asString(data.description),
      href: asString(data.href),
      variant: asString(data.variant, 'tiles'),
      size: asString(data.size, '2x1'),
      severity: asSeverity(data.severity),
      available: asBoolean(data.available, true),
      unavailable_reason: asNullableString(data.unavailable_reason),
      summary: asString(data.summary),
      metrics: Array.isArray(data.metrics) ? data.metrics.map(DashboardMetricSummary.from) : [],
      series: Array.isArray(data.series) ? data.series.map(DashboardGraphSeries.from) : [],
      warnings: Array.isArray(data.warnings) ? data.warnings.map(DashboardIssueSummary.from) : [],
      errors: Array.isArray(data.errors) ? data.errors.map(DashboardIssueSummary.from) : [],
      checked_at: asCheckedAt(data.checked_at),
    })
  }
}

export class DashboardSectionSummary implements IDashboardSectionSummary {
  id = ''
  title = ''
  description = ''
  href = ''
  severity: DashboardSeverity = 'unknown'
  warning_count = 0
  error_count = 0
  summary = ''
  metrics: DashboardMetricSummary[] = []
  warnings: DashboardIssueSummary[] = []
  errors: DashboardIssueSummary[] = []
  checked_at: number | string | null = null

  constructor(data?: Partial<IDashboardSectionSummary>) {
    if (!data) return
    this.id = data.id ?? this.id
    this.title = data.title ?? this.title
    this.description = data.description ?? this.description
    this.href = data.href ?? this.href
    this.severity = data.severity ?? this.severity
    this.warning_count = data.warning_count ?? this.warning_count
    this.error_count = data.error_count ?? this.error_count
    this.summary = data.summary ?? this.summary
    this.metrics = data.metrics ?? this.metrics
    this.warnings = data.warnings ?? this.warnings
    this.errors = data.errors ?? this.errors
    this.checked_at = data.checked_at ?? this.checked_at
  }

  static from(raw: unknown): DashboardSectionSummary {
    const data = asObject(raw)
    return new DashboardSectionSummary({
      id: asString(data.id),
      title: asString(data.title),
      description: asString(data.description),
      href: asString(data.href),
      severity: asSeverity(data.severity),
      warning_count: asNumber(data.warning_count),
      error_count: asNumber(data.error_count),
      summary: asString(data.summary),
      metrics: Array.isArray(data.metrics) ? data.metrics.map(DashboardMetricSummary.from) : [],
      warnings: Array.isArray(data.warnings) ? data.warnings.map(DashboardIssueSummary.from) : [],
      errors: Array.isArray(data.errors) ? data.errors.map(DashboardIssueSummary.from) : [],
      checked_at: asCheckedAt(data.checked_at),
    })
  }
}

export class DashboardOverview implements IDashboardOverview {
  overall_status: DashboardSeverity = 'unknown'
  warning_count = 0
  error_count = 0
  checked_at: number | string | null = null
  attention: DashboardAttentionItem[] = []
  sections: DashboardSectionSummary[] = []
  cards: DashboardCardSummary[] = []

  constructor(data?: Partial<IDashboardOverview>) {
    if (!data) return
    this.overall_status = data.overall_status ?? this.overall_status
    this.warning_count = data.warning_count ?? this.warning_count
    this.error_count = data.error_count ?? this.error_count
    this.checked_at = data.checked_at ?? this.checked_at
    this.attention = data.attention ?? this.attention
    this.sections = data.sections ?? this.sections
    this.cards = data.cards ?? this.cards
  }

  static from(raw: unknown): DashboardOverview {
    const data = asObject(raw)
    return new DashboardOverview({
      overall_status: asSeverity(data.overall_status),
      warning_count: asNumber(data.warning_count),
      error_count: asNumber(data.error_count),
      checked_at: asCheckedAt(data.checked_at),
      attention: Array.isArray(data.attention) ? data.attention.map(DashboardAttentionItem.from) : [],
      sections: Array.isArray(data.sections) ? data.sections.map(DashboardSectionSummary.from) : [],
      cards: Array.isArray(data.cards) ? data.cards.map(DashboardCardSummary.from) : [],
    })
  }
}
